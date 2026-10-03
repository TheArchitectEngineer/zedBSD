/* linux-c2-replay: run zedBSD's compute batch (L-MI / L-C0 / L-C2) on Linux i915
 * via direct ioctls, with the objects softpinned at the same GPU VAs zedBSD uses,
 * so the batch bytes are identical.  Linux owns GT/engine/LRC/PPGTT/execlists;
 * we bring the batch, kernel, IDD and state. */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <drm/drm.h>
#include <drm/i915_drm.h>

#define SHARED_VA 0x100400000ULL
#define BATCH_VA  0x100600000ULL
#define KSP_OFFSET 1024U
#define IDD_OFFSET 896U
#define READY_OFF 0xc00U
#define EU_OFF    0xc20U
#define DONE_OFF  0xc28U
#define CS_OFF    0xc30U
#define READY_TAG 0xc0ffee10U
#define DONE_TAG  0xc0ffee20U
#define CS_TAG    0xc0ffee30U
#define MAXTHREADS 559U   /* 112 * 5 DSS - 1 */

/* refcs_empty: SIMD8, unconditional... store-less, mov r127 r0; send.ts EOT. */
static const uint32_t empty_cs[] = {
    0x80030061U, 0x7f050220U, 0x00460005U, 0x00000000U,
    0x80030131U, 0x00000004U, 0x70007f0cU, 0x00000000U,
};
/* C1 store kernel: unconditional A64 store of 0xc0ffee02 to 0x100400c20, then EOT. */
static const uint32_t store_cs[] = {
    0x00030061U, 0x05054220U, 0x00000000U, 0xc0ffee02U,
    0x80030061U, 0x7f050220U, 0x00460005U, 0x00000000U,
    0x80030061U, 0x01264aa0U, 0x00000000U, 0x00000001U,
    0x80030161U, 0x01064aa0U, 0x00000000U, 0x00400c20U,
    0x80000101U, 0x00000000U, 0x00000000U, 0x00000000U,
    0x00030061U, 0x03260660U, 0x00000124U, 0x00000000U,
    0x00030161U, 0x03060660U, 0x00000104U, 0x00000000U,
    0x00039031U, 0x00000000U, 0xcdfa0314U, 0x019a050cU,
    0x80030131U, 0x00000004U, 0x70007f0cU, 0x00000000U,
};

static int fd;

static uint32_t gem_create(uint64_t size) {
    struct drm_i915_gem_create c = { .size = size };
    if (ioctl(fd, DRM_IOCTL_I915_GEM_CREATE, &c)) { perror("gem_create"); exit(1); }
    return c.handle;
}
static volatile uint32_t *gem_mmap(uint32_t h, uint64_t size) {
    struct drm_i915_gem_mmap_offset mo = { .handle=h, .flags=I915_MMAP_OFFSET_WB };
    if (ioctl(fd, DRM_IOCTL_I915_GEM_MMAP_OFFSET, &mo)) { perror("mmap_offset"); exit(1); }
    void *p = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mo.offset);
    if (p==MAP_FAILED) { perror("mmap"); exit(1); }
    return (volatile uint32_t *)p;
}
static uint32_t ctx_create_render(void) {
    struct { struct i915_context_param_engines base; struct i915_engine_class_instance e[1]; } eng = {0};
    eng.e[0].engine_class = I915_ENGINE_CLASS_RENDER;
    eng.e[0].engine_instance = 0;
    struct drm_i915_gem_context_create_ext_setparam sp = {0};
    sp.base.name = I915_CONTEXT_CREATE_EXT_SETPARAM;
    sp.param.param = I915_CONTEXT_PARAM_ENGINES;
    sp.param.value = (uint64_t)(uintptr_t)&eng;
    sp.param.size = sizeof(eng);
    struct drm_i915_gem_context_create_ext cc = {0};
    cc.flags = I915_CONTEXT_CREATE_FLAGS_USE_EXTENSIONS;
    cc.extensions = (uint64_t)(uintptr_t)&sp;
    if (ioctl(fd, DRM_IOCTL_I915_GEM_CONTEXT_CREATE_EXT, &cc)) { perror("ctx_create"); exit(1); }
    return cc.ctx_id;
}

/* --- batch emit (ported from i915_compute_build_batch) --- */
static uint32_t *B;
static unsigned BN;
static void e(uint32_t v){ B[BN++] = v; }
static void pc(uint32_t flags){ /* 6-DW PIPE_CONTROL, HDC pipeline flush when RT flush set */
    e(0x7a000004U | ((flags & (1u<<12)) ? (1u<<9) : 0u)); e(flags); e(0);e(0);e(0);e(0);
}
#define PC_CS_STALL (1u<<20)
#define PC_RT_FLUSH (1u<<12)
#define PC_DEPTH_FLUSH (1u<<0)
#define PC_DC_FLUSH (1u<<5)
#define PC_FLUSH_ENABLE (1u<<7)
#define PC_STATE_INV (1u<<2)
#define PC_CONST_INV (1u<<3)
#define PC_TEXTURE_INV (1u<<10)
#define PC_INSTR_INV (1u<<11)
#define PC_SCOREBOARD (1u<<1)
#define PSEL(p) (0x69040000U | (0x13U<<8) | (1u<<4) | (p))

static void emit_sba(uint64_t base){
    uint32_t mocs = 6u; /* GEN12_MOCS(3) */
    e((0x6101U<<16)|(22u-2u));           /* STATE_BASE_ADDRESS, 22 dw */
    e(1u|(mocs<<4)); e(0);               /* general */
    e(mocs<<16);                         /* stateless data-port MOCS */
    e(1u|(mocs<<4)|((uint32_t)base & 0xfffff000u)); e((uint32_t)(base>>32)); /* surface */
    e(1u|(mocs<<4)|((uint32_t)base & 0xfffff000u)); e((uint32_t)(base>>32)); /* dynamic */
    e(1u|(mocs<<4)); e(0);               /* indirect */
    e(1u|(4u<<4)|((uint32_t)base & 0xfffff000u)); e((uint32_t)(base>>32)); /* instruction: WB MOCS=GEN12_MOCS(2)=4 */
    e(1u|(0xfffffu<<12)); e(1u|(0xfffffu<<12)); e(1u|(0xfffffu<<12)); e(1u|(0xfffffu<<12)); /* sizes */
    e(1u|(mocs<<4)|((uint32_t)base & 0xfffff000u)); e((uint32_t)(base>>32)); /* bindless surface */
    e((4096u/64u-1u)<<12);
    e(1u|(mocs<<4)); e(0); e(0);
}
static void marker(uint64_t va, uint32_t tag){ e(0x10000002U); e((uint32_t)va); e((uint32_t)(va>>32)); e(tag); }

/* mode: 0=L-MI, 1=L-C0(no walker), 2=L-C2(walker) */
static unsigned build(uint32_t *buf, int mode){
    B=buf; BN=0;
    uint64_t done_va = SHARED_VA + DONE_OFF;
    if (mode==0){ /* L-MI: just a PPGTT marker + BB_END */
        marker(SHARED_VA+READY_OFF, READY_TAG);
        marker(SHARED_VA+DONE_OFF, DONE_TAG);
        e(0x05000000U); /* MI_BATCH_BUFFER_END */
        e(0);
        return BN;
    }
    pc(PC_CS_STALL|PC_RT_FLUSH|PC_DEPTH_FLUSH|PC_DC_FLUSH|PC_FLUSH_ENABLE);
    e(PSEL(0));                          /* PIPELINE_SELECT 3D */
    emit_sba(SHARED_VA);
    pc(PC_CS_STALL|PC_STATE_INV|PC_CONST_INV|PC_TEXTURE_INV|PC_INSTR_INV);
    pc(PC_CS_STALL|PC_RT_FLUSH|PC_DEPTH_FLUSH);
    e(PSEL(2));                          /* PIPELINE_SELECT GPGPU */
    pc(PC_CS_STALL|PC_SCOREBOARD);
    e(0x70000007U); e(0);e(0); e((MAXTHREADS<<16)|(2u<<8)); e(0); e(2u<<16); e(0);e(0);e(0); /* MEDIA_VFE_STATE */
    e(0x70040000U); e(0);                /* MEDIA_STATE_FLUSH */
    e(0x70020002U); e(0); e(32u); e(IDD_OFFSET); /* MIDL */
    marker(SHARED_VA+READY_OFF, READY_TAG);
    if (mode>=2){
        e(0x7105000dU); e(0);e(0);e(0); e(0); e(0);e(0); e(1u); e(0);e(0); e(1u); e(0); e(1u); e(0x1u); e(0xffffffffU); /* GPGPU_WALKER */
    } else {
        for (int i=0;i<15;i++) e(0);
    }
    e(0x70040000U); e(0);                /* MEDIA_STATE_FLUSH */
    pc(PC_CS_STALL|PC_DC_FLUSH|PC_FLUSH_ENABLE); /* PC-A */
    e(0x7a000004U); e(PC_CS_STALL|(1u<<14)); e((uint32_t)done_va); e((uint32_t)(done_va>>32)); e(DONE_TAG); e(0); /* PC-B post-sync */
    marker(SHARED_VA+CS_OFF, CS_TAG);
    e(0x05000000U); e(0);                /* MI_BATCH_BUFFER_END */
    return BN;
}

static int run(uint32_t ctx, uint32_t shared, uint32_t batch,
               volatile uint32_t *page, volatile uint32_t *bmap, int mode, const char *name){
    memset((void*)page,0,4096);
    if (mode==3) memcpy((uint8_t*)page + KSP_OFFSET, store_cs, sizeof(store_cs));
    else memcpy((uint8_t*)page + KSP_OFFSET, empty_cs, sizeof(empty_cs));
    volatile uint32_t *idd = page + IDD_OFFSET/4;
    idd[0]=KSP_OFFSET; idd[2]=1u<<20; idd[6]=1u;
    page[READY_OFF/4]=0xdead0000; page[EU_OFF/4]=0xdead0000; page[DONE_OFF/4]=0xdead0000; page[CS_OFF/4]=0xdead0000;

    uint32_t bat[1024];
    unsigned n = build(bat, mode);
    memcpy((void*)bmap, bat, n*4);
    __sync_synchronize();

    struct drm_i915_gem_exec_object2 obj[2] = {0};
    obj[0].handle = shared; obj[0].offset = SHARED_VA;
    obj[0].flags = EXEC_OBJECT_PINNED | EXEC_OBJECT_SUPPORTS_48B_ADDRESS | EXEC_OBJECT_WRITE;
    obj[1].handle = batch; obj[1].offset = BATCH_VA;
    obj[1].flags = EXEC_OBJECT_PINNED | EXEC_OBJECT_SUPPORTS_48B_ADDRESS;
    struct drm_i915_gem_execbuffer2 eb = {0};
    eb.buffers_ptr = (uint64_t)(uintptr_t)obj;
    eb.buffer_count = 2;
    eb.batch_len = n*4;
    eb.flags = 0; /* engine index 0 = RENDER */
    eb.rsvd1 = ctx;
    int rc = ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &eb);
    if (rc){ printf("%s: EXECBUFFER2 rejected: %s (errno=%d)\n", name, strerror(errno), errno); return -1; }

    struct drm_i915_gem_wait w = { .bo_handle=batch, .timeout_ns=3000000000LL };
    int wr = ioctl(fd, DRM_IOCTL_I915_GEM_WAIT, &w);
    int waited = (wr==0);
    if (wr) printf("%s: GEM_WAIT rc=%d %s\n", name, wr, strerror(errno));

    __sync_synchronize();
    printf("%s: waited=%d ready=0x%08x eu=0x%08x done=0x%08x cs=0x%08x  batch_dw=%u\n",
        name, waited, page[READY_OFF/4], page[EU_OFF/4], page[DONE_OFF/4], page[CS_OFF/4], n);
    return waited ? 0 : -1;
}

int main(void){
    const char *nodes[] = {"/dev/dri/renderD128","/dev/dri/card0"};
    for (unsigned i=0;i<2;i++){ fd=open(nodes[i],O_RDWR); if(fd>=0){ printf("opened %s\n",nodes[i]); break; } }
    if (fd<0){ perror("open drm"); return 1; }
    uint32_t ctx = ctx_create_render();
    printf("ctx=%u\n", ctx);
    uint32_t shared = gem_create(4096), batch = gem_create(4096);
    volatile uint32_t *page = gem_mmap(shared, 4096);
    volatile uint32_t *bmap = gem_mmap(batch, 4096);
    printf("shared_h=%u batch_h=%u  softpin shared@0x%llx batch@0x%llx\n", shared, batch,
        (unsigned long long)SHARED_VA, (unsigned long long)BATCH_VA);

    if (run(ctx, shared, batch, page, bmap, 0, "L-MI")) { printf("STOP: L-MI failed\n"); return 0; }
    if (run(ctx, shared, batch, page, bmap, 1, "L-C0")) { printf("STOP: L-C0 failed\n"); return 0; }
    run(ctx, shared, batch, page, bmap, 2, "L-C2");
    run(ctx, shared, batch, page, bmap, 3, "L-C1");
    return 0;
}
