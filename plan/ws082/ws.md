<!-- awesome-plan project=zedbsd record=ws082 -->

# WS082: Linux の `/dev/kvm` の移植の検討

<!-- awesome-plan-current:start -->
Status: planning
Primary Milestone: MG002
Related Milestones: —
Objectives: O1
Parent: [Master](../master.md)
Queue: なし
Resume point: p001（検討）から。**まずは検討だけ**（実装は検討の結果をユーザーが判断してから）
<!-- awesome-plan-current:end -->

## 目標（2026-09-28 ユーザー）

「Linuxの/dev/kvmの移植を目指します。ただし、eventfdなどの非POSIXなFDはzedBSDカーネルにはないため、MMIOやIRQの通知は、通常のunix socketでの
メッセージ受信に置き換える必要があります。なので、まずは検討だけを行って、KVMのioctlのどの範囲は直接移植できて、どの範囲は同じioctlで別な
メカニズムで代替でき、何か実装不能な機能があるか、を検討しましょう。」

## 検討の成果物（p001）

`plan/ws082/study.md`: KVM の API（`/dev/kvm`・VM の fd・vCPU の fd の ioctl、`kvm_run` の mmap、capability）を 1 つずつ次に分類する。

| 分類 | 意味 |
| --- | --- |
| A. 直接の移植 | 同じ ioctl・同じ意味で実装できる（例: `KVM_GET_API_VERSION`・`KVM_CREATE_VM`・`KVM_CREATE_VCPU`・`KVM_RUN`・`KVM_GET/SET_REGS`・`KVM_SET_USER_MEMORY_REGION` 等の見込み） |
| B. 同じ ioctl で別の仕組み | ioctl の形は保ち、eventfd 等の非 POSIX の fd の代わりに unix socket の message 等を使う（例: `KVM_IOEVENTFD`・`KVM_IRQFD`・`KVM_SET_GSI_ROUTING`、MMIO・PIO の exit の通知、dirty log の通知） |
| C. 実装できない・しない | zedBSD に無い Linux の仕組みに強く依る（例: `userfaultfd`、`memfd`/`guest_memfd`、SEV・TDX 等の見込み）。理由と代替の有無 |

あわせて調べること:
- **zedBSD の側に要る基盤**: VT-x（VMX）・AMD-V（SVM）の有効化と VM exit の処理、EPT・NPT の page table、guest の memory の pin と
  zedBSD の VM（`vm_object`）との関係、vCPU の thread と scheduler、割り込みの注入（APIC の仮想化、posted interrupt）、TSC・時刻。
  **HAL の責務の追加（VMX の命令・MSR・VMCS）は差分ごとのユーザーの承認が要る**ので、どの部分が HAL に要るかを明示する。
- **unix socket での通知の設計**: eventfd の代わりの message の形（MMIO・PIO の address・値、IRQ の line）、QEMU 等の利用者の側の改修の範囲、
  遅延と throughput の見積もり、`SCM_RIGHTS` で fd を渡す必要の有無。
- **利用者**: QEMU（`-accel kvm`）、firecracker・cloud-hypervisor 等がどの ioctl・capability を必須とするか（最小の集合）。
- 最初の到達点の案（例: QEMU で Linux の guest が KVM で起動する、または最小の自作の VMM で real mode の code が動く）と、Phase の分け方の案。

## Phase（案）

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| ws082-p001 | 検討（上の study.md）。実装はしない | planning | — |
| 以降 | 検討の結果をユーザーが判断してから計画する | — | p001 |
