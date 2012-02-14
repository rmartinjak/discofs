include config.mk

SRCDIR=src
OBJDIR=obj

_OBJ = fs2go.o funcs.o paths.o sync.o job.o conflict.o worker.o transfer.o db.o log.o lock.o fuseops.o queue.o bst.o hashtable.o
OBJ = $(addprefix $(OBJDIR)/,$(_OBJ))

CFLAGS += -I$(SRCDIR)/hashtable

default : fs2go

$(OBJDIR) :
	@mkdir $(OBJDIR)

$(OBJDIR)/hashtable.o : $(SRCDIR)/hashtable/hashtable.c
	@echo CC -c $<
	@$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/%.o : $(SRCDIR)/%.c
	@echo CC -c $<
	@$(CC) $(CFLAGS) -c -o $@ $<

fs2go : $(OBJDIR) $(OBJ)
	@echo CC -o $@
	@$(CC) $(CFLAGS) -o fs2go $(OBJ)

clean :
	@echo cleaning
	@rm -f fs2go
	@rm -rf $(OBJDIR)

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

.PHONY: clean install
