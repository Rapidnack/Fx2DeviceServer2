`timescale 1 ps / 1 ps

module top
(
	input wire CLK,
	
	output wire [7:0] LED
);

	reg [31:0] counter;
	
	always @(posedge CLK) begin
		counter <= counter + 1;
	end
	
	assign LED = counter[25 -: 8];
	
endmodule
