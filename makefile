src=$(wildcard ./*.cpp)
obj=$(src:%.cpp=%.o)
lib:=-lpthread

header= header
target= main
CC= g++

$(target): $(obj)
	$(CC) $^ -g -o $@ $(lib)

%.o : %.cpp
	$(CC) -c -g $< -I$(header) -o $@

.PHONY: clean
clean:
	rm $(obj) $(target)