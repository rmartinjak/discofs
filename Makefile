include config.mk

SRCDIR=src
OBJDIR=obj
SUBOBJDIR=$(OBJDIR)/sub

_OBJ = fs2go funcs paths sync job conflict worker transfer db log lock fsops debugops queue bst
OBJ = $(addprefix $(OBJDIR)/,$(addsuffix .o,$(_OBJ)))

SUBMODULES = datastructs
SUBOBJ = $(addprefix $(OBJDIR)/,$(addsuffix .a,$(SUBMODULES)))

SUBINCLUDES = -Isrc/datastructs/src

INCLUDES += $(SUBINCLUDES)

export CC CPPFLAGS CFLAGS LDFLAGS LIBS

default : options fs2go

# directories

$(OBJDIR) :
	@mkdir $(OBJDIR)

$(SUBOBJDIR) : $(OBJDIR)
	@mkdir $(SUBOBJDIR)

$(OBJDIR)/datastructs.a :
	@make $(MAKEFLAGS) -C $(SRCDIR)/datastructs DESTDIR=$(realpath $(OBJDIR)) options archive

$(OBJDIR)/%.o : $(SRCDIR)/%.c
	@echo CC -c $<
	@$(CC) $(CPPFLAGS) $(CFLAGS) $(SUBINCLUDES) -c -o $@ $<

fs2go : $(OBJDIR) $(OBJ) $(SUBOBJ)
	@echo CC -o $@
	@$(CC) -o fs2go $(OBJ) $(SUBOBJ) $(CFLAGS) $(LDFLAGS) $(LIBS)

clean :
	@echo cleaning
	@rm -f fs2go
	@rm -rf $(OBJDIR)

options :
	@echo "CC       =" $(CC)
	@echo "CPPFLAGS =" $(CPPFLAGS)
	@echo "CFLAGS   =" $(CFLAGS)
	@echo "INCLUDES =" $(INCLUDES) $(SUBINCLUDES)
	@echo "LDFLAGS  =" $(LDFLAGS)
	@echo "LIBS     =" $(LIBS)
	@echo

install :
	@echo installing executable to ${DESTDIR}${PREFIX}/bin
	@mkdir -p ${DESTDIR}${PREFIX}/bin
	@install -m 755 -d ${DESTDIR}${PREFIX}/bin
	@install -m 755 fs2go ${DESTDIR}${PREFIX}/bin/fs2go
	@echo installing manual page to ${DESTDIR}${MANPREFIX}/man1
	@mkdir -p ${DESTDIR}${MANPREFIX}/man1
	@install -m 644 fs2go.1 ${DESTDIR}${MANPREFIX}/man1/fs2go.1
	@sed -i "s/VERSION/${VERSION}/g" ${DESTDIR}${MANPREFIX}/man1/fs2go.1

uninstall :
	@echo removing executable file from ${DESTDIR}${PREFIX}/bin
	@rm -f ${DESTDIR}${PREFIX}/bin/fs2go
	@echo removing manual page from ${DESTDIR}${MANPREFIX}/man1
	@rm -f ${DESTDIR}${MANPREFIX}/man1/fs2go.1

.PHONY: clean install options
