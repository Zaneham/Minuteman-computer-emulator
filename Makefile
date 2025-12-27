# D17B Minuteman I Guidance Computer Emulator
# Copyright 2025 Zane Hambly - Apache 2.0 License

CC = gcc
CFLAGS = -Wall -Wextra -O2 -Iinclude
LDFLAGS =

# Windows vs Unix
ifeq ($(OS),Windows_NT)
    TARGET = d17b.exe
    RM = del /Q
    MKDIR = if not exist "$(1)" mkdir "$(1)"
else
    TARGET = d17b
    RM = rm -f
    MKDIR = mkdir -p $(1)
endif

SRCDIR = src
INCDIR = include
OBJDIR = obj

SOURCES = $(SRCDIR)/d17b.c $(SRCDIR)/main.c
OBJECTS = $(OBJDIR)/d17b.o $(OBJDIR)/main.o

.PHONY: all clean test

all: $(OBJDIR) $(TARGET)

$(OBJDIR):
	$(call MKDIR,$(OBJDIR))

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)
	@echo Built $(TARGET)

$(OBJDIR)/d17b.o: $(SRCDIR)/d17b.c $(INCDIR)/d17b.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/main.o: $(SRCDIR)/main.c $(INCDIR)/d17b.h
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TARGET)
	./$(TARGET) -t

clean:
	$(RM) $(OBJDIR)/*.o $(TARGET)
