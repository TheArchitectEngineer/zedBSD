/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * bringup: the first design for the Xilinx KC705 (XC7K325T-2FFG900C), made
 * to check the FPGA workflow (build, JTAG load, JTAG read-back) without the
 * board's UART.
 *
 * Clock: the 200 MHz differential SYSCLK through IBUFDS and BUFG.  Reset:
 * the CPU_RESET push button (active high), synchronised.
 *
 * LEDs (GPIO_LED_0..7):
 *   [3:0]  follow GPIO_DIP_SW0..3
 *   [7:4]  a walking one that moves every 2^26 cycles (0.34 s); it stops
 *          while the centre button is held, and shows {N, E, S, W} while
 *          any of those buttons is held
 *   all 8  show SCRATCH[7:0] instead while SCRATCH[31] is 1 (set over JTAG)
 *
 * JTAG: a BSCANE2 on USER1 (the 7 Series IR 0x02) with a 128-bit data
 * register, shifted least significant bit first:
 *   [31:0]    ID      0x4B373035 ("K705")
 *   [63:32]   STATUS  [3:0] DIP switches, [8:4] buttons {C, W, S, E, N},
 *                     [23:16] the LEDs, [31:24] VERSION (1)
 *   [95:64]   COUNTER the 200 MHz cycle counter, Gray-coded (the host
 *                     decodes it: one bit changes at a time, so a sample
 *                     taken across the clock domains is off by at most one)
 *   [127:96]  SCRATCH the last word written
 * A scan whose bits [63:32] shifted in are WRITE_KEY (0x57524954, "WRIT")
 * sets SCRATCH to its bits [31:0] at Update-DR; other scans only read.  The
 * status and counter cross from the 200 MHz domain into TCK unsynchronised;
 * they are quasi-static or Gray-coded, and the XDC cuts those paths.
 */

`timescale 1ns / 1ps
`default_nettype none

module bringup (
	input wire sysclk_p,
	input wire sysclk_n,
	input wire cpu_reset,
	input wire [3:0] dip,
	input wire btn_n,
	input wire btn_e,
	input wire btn_s,
	input wire btn_w,
	input wire btn_c,
	output wire [7:0] led
);
	localparam [31:0] BRINGUP_ID = 32'h4B373035;
	localparam [7:0] BRINGUP_VERSION = 8'd1;
	localparam [31:0] WRITE_KEY = 32'h57524954;

	/* The 200 MHz domain. */
	wire clk_ibuf;
	wire clk;

	IBUFDS sysclk_ibuf (
		.I(sysclk_p),
		.IB(sysclk_n),
		.O(clk_ibuf)
	);
	BUFG sysclk_bufg (
		.I(clk_ibuf),
		.O(clk)
	);

	reg [1:0] rst_sync = 2'b11;
	wire rst = rst_sync[1];

	always @(posedge clk)
		rst_sync <= {rst_sync[0], cpu_reset};

	/* The switches and buttons, two flip-flops each. */
	reg [8:0] in_meta = 9'd0;
	reg [8:0] in_sync = 9'd0;

	always @(posedge clk) begin
		in_meta <= {btn_c, btn_w, btn_s, btn_e, btn_n, dip};
		in_sync <= in_meta;
	end

	wire [3:0] dip_sync = in_sync[3:0];
	wire [3:0] nesw_sync = in_sync[7:4];	/* {W, S, E, N} */
	wire btn_c_sync = in_sync[8];

	/* The cycle counter and its Gray code. */
	reg [31:0] count = 32'd0;
	reg [31:0] count_gray = 32'd0;

	always @(posedge clk) begin
		if (rst)
			count <= 32'd0;
		else
			count <= count + 32'd1;
		count_gray <= count ^ (count >> 1);
	end

	/* The walking one on LED4..7. */
	reg [25:0] tick = 26'd0;
	reg [3:0] walk = 4'b0001;

	always @(posedge clk) begin
		if (rst) begin
			tick <= 26'd0;
			walk <= 4'b0001;
		end else if (!btn_c_sync) begin
			tick <= tick + 26'd1;
			if (tick == 26'h3FFFFFF)
				walk <= {walk[2:0], walk[3]};
		end
	end

	/* SCRATCH lives in the TCK domain (below); it is static between scans. */
	wire [31:0] scratch;
	reg [7:0] led_r = 8'd0;

	always @(posedge clk) begin
		if (scratch[31])
			led_r <= scratch[7:0];
		else if (nesw_sync != 4'd0)
			led_r <= {nesw_sync[0], nesw_sync[1], nesw_sync[2],
			    nesw_sync[3], dip_sync};
		else
			led_r <= {walk, dip_sync};
	end

	assign led = led_r;

	/* The JTAG side: BSCANE2 on USER1. */
	wire tck_raw;
	wire tck;
	wire jtag_tdi;
	wire jtag_sel;
	wire jtag_capture;
	wire jtag_shift;
	wire jtag_update;
	reg [127:0] dr = 128'd0;
	reg [31:0] scratch_r = 32'd0;

	BSCANE2 #(
		.JTAG_CHAIN(1)
	) bscan (
		.CAPTURE(jtag_capture),
		.DRCK(),
		.RESET(),
		.RUNTEST(),
		.SEL(jtag_sel),
		.SHIFT(jtag_shift),
		.TCK(tck_raw),
		.TDI(jtag_tdi),
		.TMS(),
		.UPDATE(jtag_update),
		.TDO(dr[0])
	);
	BUFG tck_bufg (
		.I(tck_raw),
		.O(tck)
	);

	wire [31:0] status = {BRINGUP_VERSION, led_r, 7'd0, in_sync};

	always @(posedge tck) begin
		if (jtag_sel && jtag_capture)
			dr <= {scratch_r, count_gray, status, BRINGUP_ID};
		else if (jtag_sel && jtag_shift)
			dr <= {jtag_tdi, dr[127:1]};
		if (jtag_sel && jtag_update && dr[63:32] == WRITE_KEY)
			scratch_r <= dr[31:0];
	end

	assign scratch = scratch_r;
endmodule

`default_nettype wire
