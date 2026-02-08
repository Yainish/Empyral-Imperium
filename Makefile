CXX = g++
CXXFLAGS = -Wall -O2 -std=c++17 -Iexternal/raylib/windows
LDFLAGS = external/raylib/windows/libraylib.a \
          -lopengl32 -lgdi32 -lwinmm

SRC = src/main.cpp
OUT = game.exe

$(OUT):
	$(CXX) $(SRC) -o $(OUT) $(CXXFLAGS) $(LDFLAGS)

clean:
	del $(OUT)
