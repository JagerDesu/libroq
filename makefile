CC := gcc
CFLAGS := -g -Iinclude/
LDFLAGS := -g
LDLIBS  := -lSDL2 -lSDL2main
TARGET := roq-player

BINEXT := 

BUILDDIR := bin
SOURCEDIR := src
OBJDIR := obj

include src/makefile

OBJS := $(addprefix $(OBJDIR)/, $(OBJS))


# The main target
$(BUILDDIR)/$(TARGET)$(BINEXT): $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -o $@ $(LDLIBS)

clean:
	rm -f $(OBJS) $(DEPS) $(BUILDDIR)/$(TARGET)$(BINEXT) 

# makes sure the target dir exists
MKDIR = if [ ! -d $(dir $@) ]; then mkdir -p $(dir $@); fi

$(OBJDIR)/%.o: $(SOURCEDIR)/%.c
	@$(MKDIR)
	@echo Compiling $<
	@$(CC) $(CFLAGS) -c $< -MD -MT $@ -MF $(@:%o=%d) -o $@

$(OBJDIR)/%.o: $(SOURCEDIR)/%.cpp
	@$(MKDIR)
	@echo Compiling $<
	@$(CC) $(CFLAGS) -c $< -MD -MT $@ -MF $(@:%o=%d) -o $@