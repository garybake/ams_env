CXX = g++
CXXFLAGS = -Wall -Iinclude
TARGET = ams_check.exe
DLL = capriceenv.dll
ENGINE_SRC = src/capriceenv.cpp
ENGINE_HDR = src/capriceenv.h include/libretro.h

.PHONY: all clean

all: $(TARGET) $(DLL)

$(TARGET): src/main.cpp $(ENGINE_SRC) $(ENGINE_HDR)
	$(CXX) $(CXXFLAGS) -o $(TARGET) src/main.cpp $(ENGINE_SRC)

$(DLL): $(ENGINE_SRC) $(ENGINE_HDR)
	$(CXX) $(CXXFLAGS) -DCAPRICEENV_BUILD_DLL -shared -static -o $(DLL) $(ENGINE_SRC)

clean:
	rm -f $(TARGET) $(DLL)
