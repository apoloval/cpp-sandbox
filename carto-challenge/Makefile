CPP_FLAGS=-std=c++11

define MISSING_DATASET_MSG
You have to download the dataset file first.
Follow this link and save the file in this directory with name 'tile.csv'")

	https://drive.google.com/file/d/0B_oZluOoVpAnOFJ3WHI4SnpqcXM

endef

all: tile.csv torque torque-mod

test: all
	@echo "Testing original implementation:"
	./torque tile.csv > output.ppm
	@echo "Testing modified implementation:"
	./torque-mod tile.csv > output-mod.ppm

torque: carto.cpp
	${CXX} ${CPP_FLAGS} -o torque carto.cpp

torque-mod: carto-mod.cpp
	${CXX} ${CPP_FLAGS} -o torque-mod carto-mod.cpp

tile.csv:
	$(error ${MISSING_DATASET_MSG})

.PHONY clean:
	rm -f torque torque-mod output*
