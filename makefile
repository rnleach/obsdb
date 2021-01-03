# Directory layout.
PROJDIR := $(realpath $(CURDIR)/)
SOURCEDIR := $(PROJDIR)/src
OBJDIR := $(PROJDIR)/obj
BUILDDIR := $(PROJDIR)/build
DOCDIR := $(PROJDIR)/doc

# Target library
TARGET = $(BUILDDIR)/libobs.a
TARGET_API = $(SOURCEDIR)/obs.h
TARGET_DOC = $(PROJDIR)/Doxyfile

CFLAGS = -g -fPIC -Wall -Werror -pedantic -O3 -std=c11 -I$(SOURCEDIR)

# -------------------------------------------------------------------------------------------------
# enable some time functions for POSIX
CFLAGS += -D_GNU_SOURCE

# cURL library
CFLAGS += `curl-config --cflags`

# libcsv3 library
CFLAGS +=

# sqlite3 library for download cache
CFLAGS += `pkg-config --cflags sqlite3`
# -------------------------------------------------------------------------------------------------

# Compiler and compiler options
CC = clang

# Archiver for creating the library
AR = ar -rcs $(TARGET)

# Show commands make uses
VERBOSE = TRUE

# Add this list to the VPATH, the place make will look for the source files
VPATH = $(SOURCEDIR)

# Create a list of *.c files in DIRS
SOURCES = $(wildcard $(SOURCEDIR)/*.c)

# Define object files for all sources, and dependencies for all objects
OBJS := $(subst $(SOURCEDIR), $(OBJDIR), $(SOURCES:.c=.o))
DEPS = $(OBJS:.o=.d)

# Hide or not the calls depending on VERBOSE
ifeq ($(VERBOSE),TRUE)
	HIDE = 
else
	HIDE = @
endif

.PHONY: all clean directories doc

all: makefile directories $(TARGET)

$(TARGET): directories makefile $(OBJS) 
	@echo building library $@
	$(HIDE)${AR} ${OBJS}
	cp ${TARGET_API} ${BUILDDIR}/

doc: directories makefile $(OBJS)
	@echo building documentation $@
	$(HIDE)doxygen $(TARGET_DOC)

-include $(DEPS)

# Generate rules
$(OBJDIR)/%.o: $(SOURCEDIR)/%.c makefile
	@echo Building $@
	$(HIDE)$(CC) -c $(CFLAGS) -o $@ $< -MMD

directories:
	@echo Creating directory $<
	$(HIDE)mkdir -p $(OBJDIR) 2>/dev/null
	$(HIDE)mkdir -p $(BUILDDIR) 2>/dev/null

clean:
	$(HIDE)rm -rf $(OBJDIR) $(BUILDDIR) $(DOCDIR) 2>/dev/null
	@echo Cleaning done!

detected_OS = $(shell uname)
ifeq ($(detected_OS), Linux)
	lib_dir = ~/usr/lib
	inc_dir = ~/usr/include
else
	lib_dir = ~/lib
	inc_dir = ~/include
endif

install: $(TARGET) makefile
	cp $(TARGET) $(lib_dir)/
	cp $(BUILDDIR)/obs.h $(inc_dir)/
	cp $(PROJDIR)/obs.pc $(lib_dir)/pkgconfig/
	

