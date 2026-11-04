CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -g -MMD -MP

SRCS := $(wildcard *.cpp)
OBJS := $(SRCS:.cpp=.o)

main: $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o main

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	-del /f /q $(subst /,\,$(OBJS) main.exe) 2>nul

.PHONY: clean