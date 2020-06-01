`timescale 1 ps / 1 ps

module top
(
	input wire CLK,
	
	input wire SPI_SCLK,
	input wire SPI_NSS,
	input wire SPI_MOSI,
	output wire SPI_MISO,
	
	output wire [7:0] LED,
	
	inout wire [7:0] ADDA,
	output wire ADDA_CLK,
	
	output wire IFCLK,
	inout wire [7:0] FD,
	output wire SLRDN,
	output wire SLWRN,
	input wire [2:0] FLAGN,
	output wire SLOEN,
	output wire [1:0] FIFOADR,
	output wire PKTENDN
);



	wire [31:0] pio0;
	wire [31:0] pio1;
	
	pll	pll_inst (
		.inclk0 (CLK),
		.c0 (IFCLK) // 48M
	);
	
	QsysCore u0 (
		.clk_clk                                                                                         (IFCLK),
		.reset_reset_n                                                                                   (1'b1),
		.spi_slave_to_avalon_mm_master_bridge_0_export_0_mosi_to_the_spislave_inst_for_spichain          (SPI_MOSI),
		.spi_slave_to_avalon_mm_master_bridge_0_export_0_nss_to_the_spislave_inst_for_spichain           (SPI_NSS),
		.spi_slave_to_avalon_mm_master_bridge_0_export_0_miso_to_and_from_the_spislave_inst_for_spichain (SPI_MISO),
		.spi_slave_to_avalon_mm_master_bridge_0_export_0_sclk_to_the_spislave_inst_for_spichain          (SPI_SCLK),
		.pio_0_external_connection_export                                                                (pio0),
		.pio_1_external_connection_export                                                                (pio1)
	);

	assign LED = pio0[7:0];



	reg [7:0] uadda_r;

	always @(posedge IFCLK) begin
		uadda_r <= ADDA;
	end
	assign ADDA = 8'bzzzzzzzz;
	assign ADDA_CLK = IFCLK;



	reg [25:0] counter;
	
	always @(posedge IFCLK) begin
		if (counter == pio1[25:0] - 1)
			counter <= 0;
		else
			counter <= counter + 1;
	end



	assign SLRDN = 1'b1;
	assign SLOEN = 1'b1;
	assign FIFOADR = 2'b00;
	assign PKTENDN = 1'b1;
	
	assign SLWRN = ~(FLAGN[1] & (counter == 0));
	assign FD = uadda_r;
	
endmodule