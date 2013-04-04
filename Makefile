include config.mk

SRCDIR = src
OBJDIR = obj
DOXY = Doxyfile

OBJNAMES = discofs state funcs paths sync job hardlink conflict worker transfer db log lock fsops debugops remoteops
OBJ = $(addprefix $(OBJDIR)/,$(addsuffix .o,$(OBJNAMES)))

SUBMODULES = datastructs
SUBOBJ = $(addprefix $(OBJDIR)/,$(addsuffix .a,$(SUBMODULES)))
SUBINCLUDES = -Isrc/datastructs/src

INCLUDES += $(SUBINCLUDES)

export CC CPPFLAGS CFLAGS LDFLAGS LIBS

default : all

all : options $(OBJDIR) discofs doc

$(OBJDIR) :
	@mkdir $@ || true


$(OBJDIR)/datastructs.a : recurse
	$(MAKE) $(MFLAGS) -C $(SRCDIR)/datastructs DESTDIR=$(realpath $(OBJDIR)) archive


$(OBJDIR)/%.o : $(SRCDIR)/%.c
	@echo CC -c $<
	@$(CC) $(FUSE_VERSION) $(CPPFLAGS) $(CFLAGS) $(INCLUDES) -c -o $@ $<

doc : $(DOXY) $(SRCDIR)/*.c
	@doxygen Doxyfile

discofs : $(OBJ) $(SUBOBJ)
	@echo CC -o $@
	@$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)


clean :
	@echo cleaning
	@rm -f discofs
	@rm -rf $(OBJDIR)
	@rm -rf doc/html doc/latex 


options :
	@echo "CC       =" $(CC)
	@echo "CPPFLAGS =" $(CPPFLAGS)
	@echo "CFLAGS   =" $(CFLAGS)
	@echo "INCLUDES =" $(INCLUDES)
	@echo "LDFLAGS  =" $(LDFLAGS)
	@echo "LIBS     =" $(LIBS)
	@echo


install :
	@echo installing executable to ${DESTDIR}${PREFIX}/bin
	@mkdir -p ${DESTDIR}${PREFIX}/bin
	@install -m 755 -d ${DESTDIR}${PREFIX}/bin
	@install -m 755 discofs ${DESTDIR}${PREFIX}/bin/discofs
	@echo installing manual page to ${DESTDIR}${MANPREFIX}/man1
	@mkdir -p ${DESTDIR}${MANPREFIX}/man1
	@install -m 644 discofs.1 ${DESTDIR}${MANPREFIX}/man1/discofs.1
	@sed -i "s/VERSION/${VERSION}/g" ${DESTDIR}${MANPREFIX}/man1/discofs.1


uninstall :
	@echo removing executable file from ${DESTDIR}${PREFIX}/bin
	@rm -f ${DESTDIR}${PREFIX}/bin/discofs
	@echo removing manual page from ${DESTDIR}${MANPREFIX}/man1
	@rm -f ${DESTDIR}${MANPREFIX}/man1/discofs.1


.PHONY: clean install uninstall options recurse doc
