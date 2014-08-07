TARGET=CMP
$(TARGET): CMP.o
	g++ CMP.o -o $(TARGET)
CMP.o: CMP.cpp
	g++ -c CMP.cpp
clean:
	rm -f *.o $(TARGET)