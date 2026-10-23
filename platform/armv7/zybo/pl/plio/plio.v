/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * plio: the first block between the PS and the PL of the original ZYBO
 * (XC7Z010).  It is an AXI3 slave on the PS's M_AXI_GP0 port, decoded on
 * the low 16 bits of the address (the kernel's window is 0x43C00000, 64 KiB),
 * with these 32-bit registers:
 *
 *   0x00 ID          R      0x504C494F ("PLIO")
 *   0x04 VERSION     R      1
 *   0x08 LED         RW     [3:0] = LD0..LD3
 *   0x0C INPUT       R      [3:0] = SW0..SW3, [7:4] = BTN0..BTN3
 *   0x10 SCRATCH     RW     32 bits
 *   0x14 COUNTER     R      FCLK_CLK0 cycles, free-running
 *   0x18 IRQ_STATUS  R/W1C  [0] = a button was pressed, [1] = software
 *   0x1C IRQ_ENABLE  RW     [1:0]
 *   0x20 IRQ_SET     W1S    [1] sets IRQ_STATUS[1]
 *   other            R      0; writes are ignored
 *
 * IRQ_F2P[0] is the level OR(IRQ_STATUS & IRQ_ENABLE).  Every access is
 * answered OKAY.  A burst is not expected (the kernel makes single 32-bit
 * accesses) but does not stall the bus: each beat of it reads or writes the
 * register at the burst's first address.  Writes honour WSTRB.  The clock is
 * FCLK_CLK0 (100 MHz from ps7_init) and the reset FCLK_RESET0_N.
 */

`timescale 1ns / 1ps
`default_nettype none

module plio (
	output wire [3:0] led,
	input wire [3:0] sw,
	input wire [3:0] btn
);
	localparam [31:0] PLIO_ID = 32'h504C494F;
	localparam [31:0] PLIO_VERSION = 32'd1;

	localparam [13:0] REG_ID = 14'h00;
	localparam [13:0] REG_VERSION = 14'h01;
	localparam [13:0] REG_LED = 14'h02;
	localparam [13:0] REG_INPUT = 14'h03;
	localparam [13:0] REG_SCRATCH = 14'h04;
	localparam [13:0] REG_COUNTER = 14'h05;
	localparam [13:0] REG_IRQ_STATUS = 14'h06;
	localparam [13:0] REG_IRQ_ENABLE = 14'h07;
	localparam [13:0] REG_IRQ_SET = 14'h08;

	/* The PS's clock and reset for the PL. */
	wire [3:0] fclk;
	wire [3:0] fclk_resetn;
	wire clk;
	BUFG fclk0_bufg (.I(fclk[0]), .O(clk));

	/* M_AXI_GP0 (AXI3, 12-bit IDs, 4-bit lengths). */
	wire [31:0] awaddr;
	wire [11:0] awid;
	wire awvalid;
	wire awready;
	wire [31:0] wdata;
	wire [3:0] wstrb;
	wire wlast;
	wire wvalid;
	wire wready;
	wire [11:0] bid;
	wire [1:0] bresp;
	wire bvalid;
	wire bready;
	wire [31:0] araddr;
	wire [11:0] arid;
	wire [3:0] arlen;
	wire arvalid;
	wire arready;
	wire [31:0] rdata;
	wire [11:0] rid;
	wire [1:0] rresp;
	wire rlast;
	wire rvalid;
	wire rready;
	wire irq;

	PS7 ps7 (
		.FCLKCLK(fclk),
		.FCLKRESETN(fclk_resetn),
		.IRQF2P({19'd0, irq}),
		.MAXIGP0ACLK(clk),
		.MAXIGP0AWADDR(awaddr),
		.MAXIGP0AWID(awid),
		.MAXIGP0AWVALID(awvalid),
		.MAXIGP0AWREADY(awready),
		.MAXIGP0WDATA(wdata),
		.MAXIGP0WSTRB(wstrb),
		.MAXIGP0WLAST(wlast),
		.MAXIGP0WVALID(wvalid),
		.MAXIGP0WREADY(wready),
		.MAXIGP0BID(bid),
		.MAXIGP0BRESP(bresp),
		.MAXIGP0BVALID(bvalid),
		.MAXIGP0BREADY(bready),
		.MAXIGP0ARADDR(araddr),
		.MAXIGP0ARID(arid),
		.MAXIGP0ARLEN(arlen),
		.MAXIGP0ARVALID(arvalid),
		.MAXIGP0ARREADY(arready),
		.MAXIGP0RDATA(rdata),
		.MAXIGP0RID(rid),
		.MAXIGP0RRESP(rresp),
		.MAXIGP0RLAST(rlast),
		.MAXIGP0RVALID(rvalid),
		.MAXIGP0RREADY(rready)
	);

	/* FCLK_RESET0_N comes from the PS's domain: take it in twice. */
	reg [1:0] rst_sync = 2'b00;
	wire rst_n = rst_sync[1];

	always @(posedge clk)
		rst_sync <= {rst_sync[0], fclk_resetn[0]};

	/* The registers. */
	reg [3:0] led_r = 4'd0;
	reg [31:0] scratch = 32'd0;
	reg [31:0] counter = 32'd0;
	reg [1:0] irq_status = 2'd0;
	reg [1:0] irq_enable = 2'd0;
	reg [7:0] in_s1 = 8'd0;
	reg [7:0] in_s2 = 8'd0;
	reg [3:0] btn_prev = 4'd0;

	assign led = led_r;
	assign irq = |(irq_status & irq_enable);

	/* Write channel: one burst at a time, the data after its address. */
	reg aw_full = 1'b0;
	reg [13:0] aw_reg = 14'd0;
	reg [11:0] aw_id = 12'd0;
	reg b_valid = 1'b0;
	reg [11:0] b_id = 12'd0;
	wire w_fire = wvalid && wready;

	assign awready = rst_n && !aw_full && !b_valid;
	assign wready = aw_full;
	assign bvalid = b_valid;
	assign bid = b_id;
	assign bresp = 2'b00;

	always @(posedge clk) begin
		if (!rst_n) begin
			aw_full <= 1'b0;
			b_valid <= 1'b0;
		end else begin
			if (awvalid && awready) begin
				aw_full <= 1'b1;
				aw_reg <= awaddr[15:2];
				aw_id <= awid;
			end
			if (w_fire && wlast) begin
				aw_full <= 1'b0;
				b_valid <= 1'b1;
				b_id <= aw_id;
			end
			if (b_valid && bready)
				b_valid <= 1'b0;
		end
	end

	/* Read channel: the data is taken when a beat starts and held. */
	reg r_active = 1'b0;
	reg [13:0] r_reg = 14'd0;
	reg [11:0] r_id = 12'd0;
	reg [3:0] r_left = 4'd0;
	reg [31:0] r_data = 32'd0;
	wire [13:0] r_sel = r_active ? r_reg : araddr[15:2];
	reg [31:0] r_mux;

	always @(*) begin
		case (r_sel)
		REG_ID: r_mux = PLIO_ID;
		REG_VERSION: r_mux = PLIO_VERSION;
		REG_LED: r_mux = {28'd0, led_r};
		REG_INPUT: r_mux = {24'd0, in_s2};
		REG_SCRATCH: r_mux = scratch;
		REG_COUNTER: r_mux = counter;
		REG_IRQ_STATUS: r_mux = {30'd0, irq_status};
		REG_IRQ_ENABLE: r_mux = {30'd0, irq_enable};
		default: r_mux = 32'd0;
		endcase
	end

	assign arready = rst_n && !r_active;
	assign rvalid = r_active;
	assign rdata = r_data;
	assign rid = r_id;
	assign rresp = 2'b00;
	assign rlast = r_left == 4'd0;

	always @(posedge clk) begin
		if (!rst_n) begin
			r_active <= 1'b0;
		end else if (arvalid && arready) begin
			r_active <= 1'b1;
			r_reg <= araddr[15:2];
			r_id <= arid;
			r_left <= arlen;
			r_data <= r_mux;
		end else if (r_active && rready) begin
			if (r_left == 4'd0) begin
				r_active <= 1'b0;
			end else begin
				r_left <= r_left - 4'd1;
				r_data <= r_mux;
			end
		end
	end

	/* Register updates: the inputs, the counter, the interrupt bits and
	 * the write beats. */
	wire [31:0] wmask = {{8{wstrb[3]}}, {8{wstrb[2]}}, {8{wstrb[1]}}, {8{wstrb[0]}}};
	wire [31:0] wbits = wdata & wmask;
	wire pressed = |(in_s2[7:4] & ~btn_prev);
	wire [1:0] irq_clear = (w_fire && aw_reg == REG_IRQ_STATUS) ? wbits[1:0] : 2'd0;
	wire [1:0] irq_set = {w_fire && aw_reg == REG_IRQ_SET && wbits[1], pressed};

	always @(posedge clk) begin
		in_s1 <= {btn, sw};
		in_s2 <= in_s1;
		if (!rst_n) begin
			led_r <= 4'd0;
			scratch <= 32'd0;
			counter <= 32'd0;
			irq_status <= 2'd0;
			irq_enable <= 2'd0;
			btn_prev <= in_s2[7:4];
		end else begin
			counter <= counter + 32'd1;
			btn_prev <= in_s2[7:4];
			irq_status <= (irq_status & ~irq_clear) | irq_set;
			if (w_fire) begin
				case (aw_reg)
				REG_LED: led_r <= (led_r & ~wmask[3:0]) | wbits[3:0];
				REG_SCRATCH: scratch <= (scratch & ~wmask) | wbits;
				REG_IRQ_ENABLE: irq_enable <= (irq_enable & ~wmask[1:0]) | wbits[1:0];
				default: ;
				endcase
			end
		end
	end
endmodule

`default_nettype wire
