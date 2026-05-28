CXX     := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -pthread
TARGET  := bhop
SRCS    := bhop.cpp memory.cpp

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRCS) memory.hpp client.hpp
	$(CXX) $(CXXFLAGS) $(SRCS) -o $(TARGET)
	@echo "[+] Compilado: ./$(TARGET)"

clean:
	rm -f $(TARGET)
