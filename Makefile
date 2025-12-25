CXX=g++
CXXFLAGS=-std=c++17 -O2 -Wall
LDFLAGS=-lgpiod -lpthread

DIRECTORY_INSTALL=/opt/pidp11
TARGET=frontpanel

SOURCES=frontpanel.cpp \
       gpio.cpp \
       configuration.cpp \
       logger.cpp \
       daemon.cpp \
       sim_frontpanel.c \
       sim_sock.c

# Replace *.cpp/*.c with *.o
OBJECT_FILES=$(addsuffix .o,$(basename $(SOURCES)))

all: $(TARGET)

$(TARGET): $(OBJECT_FILES)
	$(CXX) $(OBJECT_FILES) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECT_FILES) $(TARGET)

install: $(TARGET)
	install -m 755 $(TARGET) $(DIRECTORY_INSTALL)

uninstall:
	rm -f $(DIRECTORY_INSTALL)/$(TARGET)
