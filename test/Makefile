# Host (Mac/Linux) build and tests for the weather forecast module. Nothing here touches the ESP32.
#
#   make deps      once: download the ArduinoJson single header into vendor/ (needs curl)
#   make test      build and run the unit tests
#   make cli       build ./weather-cli (see weather-forecast.h for examples)
#   make fixtures  regenerate data/synthetic/*.json from the real capture (needs ruby)
#   make clean
#
# To use an ArduinoJson you already have (for example the Arduino library) instead of `make deps`:
#   make test ARDUINOJSON_DIR=$HOME/Documents/Arduino/libraries/ArduinoJson/src
CXX            ?= c++
CXXFLAGS       ?= -std=c++11 -Wall -Wextra -O1 -g
SRC            = ../src/street-cleaning-revealed
ARDUINOJSON_DIR ?= vendor
ARDUINOJSON_VERSION = v7.4.3
CORE           = $(SRC)/weather-forecast.cpp
HEADERS        = $(SRC)/weather-forecast.h
INCLUDES       = -I$(SRC) -I$(ARDUINOJSON_DIR)

.PHONY: all test cli deps fixtures clean
all: test

test: weather-forecast-test
	./weather-forecast-test

cli: weather-cli

weather-forecast-test: weather-forecast-test.cpp $(CORE) $(HEADERS) $(ARDUINOJSON_DIR)/ArduinoJson.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -DDATA_DIR='"data"' -o $@ weather-forecast-test.cpp $(CORE)

weather-cli: weather-cli.cpp $(CORE) $(HEADERS) $(ARDUINOJSON_DIR)/ArduinoJson.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ weather-cli.cpp $(CORE)

deps: vendor/ArduinoJson.h

vendor/ArduinoJson.h:
	mkdir -p vendor
	curl -fsSL -o $@ https://github.com/bblanchon/ArduinoJson/releases/download/$(ARDUINOJSON_VERSION)/ArduinoJson-$(ARDUINOJSON_VERSION).h

fixtures:
	ruby make-synthetic-fixtures.rb

clean:
	rm -f weather-forecast-test weather-cli
