CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -g
SRC = bscs25046_projectmain.cpp
OUT = ttdb

all:
	$(CXX) $(CXXFLAGS) $(SRC) -o $(OUT)

run: all
	./$(OUT)

clean:
	rm -f $(OUT)