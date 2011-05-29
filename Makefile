CC:=gcc
EXE:=snatch
OBJ:=main.o
LIB:=magic
INC:=include
CFLAGS:=-D_FILE_OFFSET_BITS=64 $(addprefix -I, $(INC)) -MMD -W -Wall -g -O2

.PHONY: clean distclean

$(EXE): $(OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(addprefix -l, $(LIB))

clean:
	-$(RM) $(OBJ)

distclean: clean
	-$(RM) $(OBJ:.o=.d) $(EXE)
