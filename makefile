CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -g -MMD -MP
LDFLAGS  := -pthread

SRCS := $(wildcard *.cpp)
OBJS := $(SRCS:.cpp=.o)
DEPS := $(OBJS:.o=.d)

main: $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) $(LDFLAGS) -o main

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

-include $(DEPS)

ifeq ($(OS),Windows_NT)
clean:
	-del /f /q $(subst /,\,$(OBJS) $(DEPS) main.exe) 2>nul
else
clean:
	rm -f $(OBJS) $(DEPS) main
endif

.PHONY: clean