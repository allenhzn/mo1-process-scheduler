CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -g -MMD -MP -O2

SRCS := $(wildcard *.cpp)
OBJS := $(SRCS:.cpp=.o)

main: $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o main

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) main

.PHONY: clean