/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host simulation of plio (platform/armv7/zybo/pl/plio/plio.v) with Icarus
 * Verilog: stand-ins for PS7 and BUFG drive the M_AXI_GP0 port the way the PS
 * does, and the checks follow the register table and design §7.5.
 *   platform/armv7/zybo/pl/tests/plio-tb.sh
 */

`timescale 1ns / 1ps

module BUFG (input wire I, output wire O);
	assign O = I;
endmodule

/* The PS's side of the ports plio uses; the test drives the regs. */
module PS7 (
	output reg [3:0] FCLKCLK,
	output reg [3:0] FCLKRESETN,
	input wire [19:0] IRQF2P,
	input wire MAXIGP0ACLK,
	output reg [31:0] MAXIGP0AWADDR,
	output reg [11:0] MAXIGP0AWID,
	output reg MAXIGP0AWVALID,
	input wire MAXIGP0AWREADY,
	output reg [31:0] MAXIGP0WDATA,
	output reg [3:0] MAXIGP0WSTRB,
	output reg MAXIGP0WLAST,
	output reg MAXIGP0WVALID,
	input wire MAXIGP0WREADY,
	input wire [11:0] MAXIGP0BID,
	input wire [1:0] MAXIGP0BRESP,
	input wire MAXIGP0BVALID,
	output reg MAXIGP0BREADY,
	output reg [31:0] MAXIGP0ARADDR,
	output reg [11:0] MAXIGP0ARID,
	output reg [3:0] MAXIGP0ARLEN,
	output reg MAXIGP0ARVALID,
	input wire MAXIGP0ARREADY,
	input wire [31:0] MAXIGP0RDATA,
	input wire [11:0] MAXIGP0RID,
	input wire [1:0] MAXIGP0RRESP,
	input wire MAXIGP0RLAST,
	input wire MAXIGP0RVALID,
	output reg MAXIGP0RREADY
);
	initial begin
		FCLKCLK = 0;
		FCLKRESETN = 0;
		MAXIGP0AWVALID = 0;
		MAXIGP0WVALID = 0;
		MAXIGP0WLAST = 0;
		MAXIGP0BREADY = 0;
		MAXIGP0ARVALID = 0;
		MAXIGP0ARLEN = 0;
		MAXIGP0RREADY = 0;
	end
	always #5 FCLKCLK[0] = ~FCLKCLK[0];
endmodule

module tb;
	reg [3:0] sw = 4'b1010;
	reg [3:0] btn = 4'b0000;
	wire [3:0] led;
	integer failures = 0;
	reg [31:0] v;
	reg [31:0] v2;
	reg [11:0] id;

	plio dut (.led(led), .sw(sw), .btn(btn));

	`define P dut.ps7
	`define CLK @(posedge dut.clk)

	task check(input [255:0] what, input [31:0] got, input [31:0] want);
		begin
			if (got !== want) begin
				$display("FAIL %0s: 0x%08x, want 0x%08x", what, got, want);
				failures = failures + 1;
			end else begin
				$display("ok   %0s: 0x%08x", what, got);
			end
		end
	endtask

	/* One read burst of len+1 beats; returns the last beat and its ID. */
	task axi_read(input [31:0] addr, input [3:0] len, input [11:0] arid,
		output [31:0] data, output [11:0] rid);
		integer beats;
		begin
			`P.MAXIGP0ARADDR <= addr;
			`P.MAXIGP0ARID <= arid;
			`P.MAXIGP0ARLEN <= len;
			`P.MAXIGP0ARVALID <= 1;
			`CLK;
			while (!`P.MAXIGP0ARREADY) `CLK;
			`P.MAXIGP0ARVALID <= 0;
			`P.MAXIGP0RREADY <= 1;
			beats = 0;
			forever begin
				`CLK;
				if (`P.MAXIGP0RVALID) begin
					beats = beats + 1;
					data = `P.MAXIGP0RDATA;
					rid = `P.MAXIGP0RID;
					if (`P.MAXIGP0RRESP !== 2'b00) begin
						$display("FAIL RRESP %b", `P.MAXIGP0RRESP);
						failures = failures + 1;
					end
					if (`P.MAXIGP0RLAST)
						disable axi_read;
				end
			end
		end
	endtask

	/* One write burst; data comes before the address when early is set. */
	task axi_write(input [31:0] addr, input [31:0] data, input [3:0] strb,
		input [11:0] awid, input early, output [11:0] bid);
		begin
			`P.MAXIGP0WDATA <= data;
			`P.MAXIGP0WSTRB <= strb;
			`P.MAXIGP0WLAST <= 1;
			`P.MAXIGP0WVALID <= 1;
			if (early) begin
				repeat (3) `CLK;
			end
			`P.MAXIGP0AWADDR <= addr;
			`P.MAXIGP0AWID <= awid;
			`P.MAXIGP0AWVALID <= 1;
			`CLK;
			while (!`P.MAXIGP0AWREADY) `CLK;
			`P.MAXIGP0AWVALID <= 0;
			while (!`P.MAXIGP0WREADY) `CLK;
			`CLK;
			`P.MAXIGP0WVALID <= 0;
			`P.MAXIGP0BREADY <= 1;
			while (!`P.MAXIGP0BVALID) `CLK;
			bid = `P.MAXIGP0BID;
			if (`P.MAXIGP0BRESP !== 2'b00) begin
				$display("FAIL BRESP %b", `P.MAXIGP0BRESP);
				failures = failures + 1;
			end
			`CLK;
			`P.MAXIGP0BREADY <= 0;
		end
	endtask

	initial begin
		/* Nothing answers while FCLK_RESET0_N is low. */
		repeat (5) `CLK;
		`P.MAXIGP0ARADDR <= 32'h43C00000;
		`P.MAXIGP0ARVALID <= 1;
		repeat (5) `CLK;
		check("ARREADY in reset", {31'd0, `P.MAXIGP0ARREADY}, 0);
		`P.MAXIGP0ARVALID <= 0;
		`P.FCLKRESETN <= 4'hF;
		repeat (4) `CLK;

		axi_read(32'h43C00000, 0, 12'h123, v, id);
		check("ID", v, 32'h504C494F);
		check("RID", {20'd0, id}, 12'h123);
		axi_read(32'h43C00004, 0, 0, v, id);
		check("VERSION", v, 1);
		axi_write(32'h43C00010, 32'h12345678, 4'hF, 12'h456, 0, id);
		check("BID", {20'd0, id}, 12'h456);
		axi_read(32'h43C00010, 0, 0, v, id);
		check("SCRATCH", v, 32'h12345678);
		axi_write(32'h43C00010, 32'hAABBCCDD, 4'b0101, 12'h7, 1, id);
		axi_read(32'h43C00010, 0, 0, v, id);
		check("SCRATCH WSTRB 0101, data first", v, 32'h12BB56DD);
		axi_read(32'h43C00014, 0, 0, v, id);
		repeat (10) `CLK;
		axi_read(32'h43C00014, 0, 0, v2, id);
		check("COUNTER advanced", {31'd0, v2 - v > 10 && v2 - v < 40}, 1);
		axi_write(32'h43C00008, 32'hFFFFFFF5, 4'hF, 0, 0, id);
		axi_read(32'h43C00008, 0, 0, v, id);
		check("LED register", v, 32'h5);
		check("LED pins", {28'd0, led}, 4'h5);
		axi_read(32'h43C0000C, 0, 0, v, id);
		check("INPUT", v, 32'h0A);
		axi_read(32'h43C00040, 0, 0, v, id);
		check("unused offset", v, 0);
		axi_read(32'h43C10000, 0, 0, v, id);
		check("alias above 64 KiB", v, 32'h504C494F);
		axi_read(32'h43C00000, 3, 12'hABC, v, id);
		check("burst of 4, last beat", v, 32'h504C494F);
		check("burst RID", {20'd0, id}, 12'hABC);

		/* Interrupts: software, then a button. */
		check("IRQ line idle", {31'd0, `P.IRQF2P[0]}, 0);
		axi_write(32'h43C00020, 32'h2, 4'hF, 0, 0, id);
		axi_read(32'h43C00018, 0, 0, v, id);
		check("IRQ_STATUS after IRQ_SET", v, 2);
		check("IRQ line masked", {31'd0, `P.IRQF2P[0]}, 0);
		axi_write(32'h43C0001C, 32'h3, 4'hF, 0, 0, id);
		`CLK;
		check("IRQ line enabled", {31'd0, `P.IRQF2P[0]}, 1);
		axi_write(32'h43C00018, 32'h2, 4'hF, 0, 0, id);
		`CLK;
		check("IRQ line after W1C", {31'd0, `P.IRQF2P[0]}, 0);
		btn = 4'b0100;
		repeat (6) `CLK;
		check("IRQ line after a press", {31'd0, `P.IRQF2P[0]}, 1);
		axi_read(32'h43C0000C, 0, 0, v, id);
		check("INPUT with BTN2", v, 32'h4A);
		axi_read(32'h43C00018, 0, 0, v, id);
		check("IRQ_STATUS after a press", v, 1);
		axi_write(32'h43C00018, 32'h1, 4'hF, 0, 0, id);
		repeat (6) `CLK;
		check("held button does not re-raise", {31'd0, `P.IRQF2P[0]}, 0);

		if (failures == 0)
			$display("PASS plio-tb");
		else
			$display("FAIL plio-tb: %0d failures", failures);
		$finish;
	end

	initial begin
		#200000;
		$display("FAIL plio-tb: timeout");
		$finish;
	end
endmodule
