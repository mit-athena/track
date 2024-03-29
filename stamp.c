/*
 *	$Id: stamp.c,v 4.14 1999-08-13 00:15:12 danw Exp $
 */

#ifndef lint
static char *rcsid_header_h = "$Id: stamp.c,v 4.14 1999-08-13 00:15:12 danw Exp $";
#endif

#include "mit-copyright.h"

#include "track.h"

extern char *mode_to_char(), *mode_to_fmt(), *mode_to_rfmt();
void poppath();

char type_char[] = " cdbfls*89ABCDEF";

/*
 * Place a time-stamp line in the format:
 *       type file uid.gid.mode.time
 * or a suitable derivate thereof
 */

write_statline( path, c)
char **path; struct currentness *c;
{
	char  *format, *linebuf, *name;
	int same_name;
	unsigned int type;
	struct stat fromstat, *s;
	unsigned size, curr1 = 0, extra = 0, len = 0;

	if ( cur_line >= maxlines) {
		maxlines += MAXLINES;
		size = maxlines * sizeof statfilebufs[0];
		statfilebufs = ( cur_line ?
				 realloc( statfilebufs, size) :
				 malloc( size));
		if ( ! statfilebufs) {
			sprintf( errmsg, "alloc failed: %d statfile lines\n",
				 cur_line);
			do_panic();
		}
	}
	/*
	 * set up type-dependent currency data:
	 */

	s = &c->sbuf;

	switch( type = TYPE( *s)) {
	case S_IFREG:
		curr1 = c->cksum;
		extra = TIME( *s);
		break;
	case S_IFLNK:
		curr1 = (unsigned int) c->link;
		len = strlen(c->link);
		break;
	case S_IFDIR:
		curr1 = 0;
		break;
	case S_IFBLK:
		curr1 = RDEV( *s);
		break;
	case S_IFCHR:
		curr1 = RDEV( *s);
		break;
	case S_IFSOCK:
		sprintf( errmsg, "can't track socket %s.\n", path[ ROOT]);
		do_gripe();
		return( type);
	case S_IFMT:
	default:
		sprintf( errmsg,
		"bad type for inode %ul, pathname %s.\n\tapparent type = %c\n",
		    (unsigned long)c->sbuf.st_ino, path[ ROOT],
		    mode_to_string(type));
		do_panic();
	}
	/* set up name & sortkey:
	 * root is a special case:
	 * its "relative path" is "", which dec_statfile() can't read.
	 */
	name = path[ NAME];
	if ( !*name) name = "/";

	linebuf = statfilebufs[ cur_line] = malloc( strlen( name) + len + 100);
	if ( !linebuf) {
		sprintf( errmsg, "malloc failed: %d statfile lines\n",
			 cur_line);
		do_panic();
	}

	/* if this entry's fromfile != cmpfile,
	 * the subscribing machine needs to know:
	 */
	same_name = ! strcmp( path[ NAME], c->name);

	/* to choose printing format, convert type-bits to array-index:
	 * the formats specify 3-7 arguments, according to type:
	 */
	format = mode_to_fmt(type);

	sprintf( linebuf, format, name, same_name ? '=' : '~' , curr1,
		 UID( *s), GID( *s), MODE( *s), extra);

	cur_line++;

	if ( verboseflag)
		fputs( linebuf, stderr);

	if ( same_name);
	else if ( ! lstat( path[ ROOT], &fromstat))
		type = TYPE( fromstat);
	else {
		sprintf( errmsg, "(write_statline) can't lstat %s\n",
			 path[ ROOT]);
		do_panic();
	}
	return( type);
}

void fake_link( root, name, c) char *root, *name; struct currentness *c; {

	/* it is difficult to fool write_statline(),
	 * update_file(), and curr_diff() all at once.
	 * write_statline() can't take a normal currency in this case,
	 * because it can't know the subscriber's fromroot.
	 * curr_diff() needs to see a normal link's currency,
	 * or else unneccessary link-updates will occur.
	 * update_file() can use a normal currency to make a link.
	 */
	if ( name != c->name)		/* speed hack for dec_statfile() */
		strcpy( c->name, name);

	if ( *root)
		sprintf( c->link, "%s/%s", root, name);
	else	*c->link = '\0';	/* special case for write_statline() */

	c->cksum = 0;
	clear_stat( &c->sbuf);
	c->sbuf.st_mode = S_IFLNK;
}

int stat_cmp( ain, bin) 
const void *ain, *bin;
{
	const char *a, *b;
	char c, d;

	a = *(const char **)ain;
	b = *(const char **)bin;
	/* Skip over first character which refers to file type */
	a++; b++;
	while ( *a != ' ' && *b != ' ') {
		c = (*a == '/') ? '\001' : *a; a++;
		d = (*b == '/') ? '\001' : *b; b++;
		if ( c != d)
			return( c - d);
	}
	return( *a == ' ' ? -1 : 1);
}


void sort_stat() {
	int i;

        qsort( (char *) statfilebufs, cur_line,
		sizeof( statfilebufs[ 0]), stat_cmp);

        for ( i = 0; i < cur_line; i++) {
                fputs( statfilebufs[ i], statfile);
	}
}

void sort_entries() {
	char *tail;
	Table list;
	Entry *A, *C;
	int i, j;

	list.table = NULL;
	list.shift = 0;

	/* NOTE: we assume that each entry begins with a sortkey string.
         * don't include entries[ 0] in the sort:
         */
        qsort( (char *)& entries[ 1], entrycnt - 1,
               sizeof(   entries[ 1]), strcmp);

	/* for each entry's fromfile (call it A),
	 * look for A's children amongst the subsequent entries,
	 * and add any that you find to A's exception-table.
	 * note that this may overfill the hash-table; in this event,
	 * we accept the performance-hit, and don't try to rehash.
	 */
	for (     A = &entries[ i = 1  ]; i < entrycnt; A = &entries[ ++i]) {
	    for ( C = &entries[ j = i+1]; j < entrycnt; C = &entries[ ++j]) {
		switch(  keyncmp( C->sortkey, i)) {
		case 1:  break;			/* get next A */
		case 0:  tail = C->fromfile + A->keylen;
			 while( '/' == *tail) tail++;
			 if ( A->names.table)
			     store( add_list_elt( tail, DONT_TRACK, NULL),
				    &A->names);
			 else add_list_elt( tail, DONT_TRACK, LIST( list));
		case -1: continue;	/* unlikely */
		}
		break; /* get next A */
	    }
	    /* if A doesn't already have an exception-list of names,
	     * then we've accumulated the list of descendant-entries
	     * in the list{} structure; convert it to a hash-table for A:
	     */
	    if ( ! list.table);
	    else if ( ! A->names.table) {
		     A->names.table = list.table;
		     A->names.shift = list.shift;
		     list.table = NULL;
		     list.shift = 0;
		     list2hashtable( &A->names);
	    }
	    else {
		     sprintf(errmsg, "sort_entries: internal error\n");
		     do_panic();
	    }
	}
}

/*
 * Decode a statfile line into its individual fields.
 * setup TYPE(), UID(), GID(), MODE(), TIME(), & RDEV() contents.
 */

char *
dec_statfile( line, c)
char *line; struct currentness *c;
{
	struct stat *s;
	int *extra = 0;
	char *end, *format, *name, same_name, type;
	int dummy, *curr1 = &dummy, d = 0, u = 0, g = 0, m = 0;

	/* these long-int temps are necessary for pc/rt compatibility:
	 * sscanf cannot scan into a short, though it may sometimes succeed
	 * in doing so. the difficulty is that it can't know about the
	 * target-integer's length, so it assumes that it's long.
	 * since the rt will truncate a short's addr, in order to treat
	 * it as a long, sscanf's data will often get lost.
	 * the solution is to give scanf longs, and then to convert these
	 * longs to shorts, explicitly.
	 */

	/* for speed, we laboriously parse the type-independent part
	 * of the statline without calling sscanf().
	 * thus, we avoid copying the pathname strings around,
	 * since the caller can re-use the originals,
	 * once they're broken out of the line-format.
	 */
	type = *line++;
	name = line;
	line = strchr( line, ' ');
	if ( ! line) {
		sprintf( errmsg, "garbled statfile: bad line =\n%s\n", line);
		sprintf( errmsg, "line has only one field\n");
		do_panic();
	}
	*line++ = '\0';

	/* in the  statfile, which contains only relative pathnames,
	 * "/" is the only pathname which can begin with a slash.
	 * in entries[], the root appears as "", which is more natural,
	 * because "" is "/"'s pathname relative to the mount-point fromroot.
	 * and because pushpath() prepends slashes in the right places, anyway.
	 * "" is hard to read with sscanf(), so we handle "/" specially:
	 */
	if ( '/' != *name);
	else if ( ! name[ 1]) *name = '\0';
	else {
		sprintf(errmsg, "statfile passed an absolute pathname: %s\n",
			name);
		do_gripe();
	}
	/* the subscriber needs to know whether the currency-data
	 * came from the remote fromfile or from the remote cmpfile.
	 */
	switch( same_name = *line++) {
	case '=': strcpy( c->name, name);
		  break;
	case '~': *c->name = '\0';
		  break;
	default:
		sprintf( errmsg, "garbled statfile: bad line =\n%s\n", line);
		sprintf( errmsg, "bad equality flag = %c\n", same_name);
		do_panic();
	}
	*c->link = '\0';
	c->cksum = 0;

	/*
	 * set up scanf arg's for type-dependent currency-data:
	 */

	s = &c->sbuf;
	clear_stat( s);

	switch( type) {
	case 'f':
		s->st_mode = S_IFREG;
		curr1 = ( int*)&c->cksum;
		extra = ( int*)&s->st_mtime;
		break;
	case 'l':	/* more common than dir's */
		s->st_mode = S_IFLNK;
		if ( end = strchr( line, '\n'))
			*end = '\0';
		else {
			sprintf( errmsg, "garbled statfile: bad line =\n%s\n",
				line);
			sprintf( errmsg, "line doesn't end with a newline\n");
			do_panic();
		}
		if ( !*line) fake_link( fromroot, c->name, c);
		else strcpy( c->link, line);
		break;
	case 'd':
		s->st_mode = S_IFDIR;
		curr1 = &dummy;
		break;
	case 'b':
		s->st_mode = S_IFBLK;
		curr1 = &d;
		break;
	case 'c':
		s->st_mode = S_IFCHR;
		curr1 = &d;
		break;
	default:
		sprintf( errmsg, "garbled statfile: bad line =\n%s\n", line);
		sprintf( errmsg, "first char isn't a file-type [fldbc]\n");
		do_panic();
	}
	/* if we've already parsed the line,
	 * as in S_IFLNK case, skip the sscanf call:
	 */
	if ( *(format = mode_to_rfmt(s->st_mode)))
		sscanf( line, format, curr1, &u, &g, &m, extra);

	s->st_uid   = (short) u;
	s->st_gid   = (short) g;
	s->st_mode |= (short) m & 07777;
	s->st_rdev  = (short) d;
	return( name);
}

/* the match abstractions implement a synchronized traversal
 * of the entries[] array & the statfile. both sets of data
 * are sorted by strcmp() on their sortkeys, which are made
 * via the KEYCPY macro.
 */

/* XXX: not the best, but not the worst approach either.
 * this  allows main() to call readstat() twice, with correct initialization.
 */
int prev_ent = 0;;

int cur_ent = 0;	/* not global! index into entries[]. */

void
init_next_match() {
	prev_ent = 0;
	cur_ent = 1;
}

int
get_next_match( name)
char *name;
{
	char key[ LINELEN];

	KEYCPY( key, name);

	for ( ; cur_ent < entrycnt; cur_ent++)
		switch ( keyncmp( key, cur_ent)) {
		case 0:  return( cur_ent);
		case -1: return( 0); 		/* advance name's    key */
		case 1:  continue;		/* advance cur_ent's key */
		}

	return( -1); /* out of entries */
}

/* last_match:
 * we know that path begins with entnum's fromfile,
 * but path may also match other entries.
 * for example, the path /a/b/c matches the entries /a & /a/b.
 * we want the longest entry that matches.
 * look-ahead to find last sub-list entry that matches
 * the current pathname: this works because by keyncmp,
 *	    /	is greater than		/Z...
 *	   |	matches			/a
 *	   |	is greater than		/a/a
 *	   \	matches			/a/b
 * /a/b/c  <	is greater than		/a/b/a
 *	   /	matches			/a/b/c
 *	   |	is less than		/a/b/c/a
 *	   |	is less than		/a/b/d
 *	    \	is less than		/a/c...
 * NOTE that the right-hand column is sorted by key,
 * and that the last match is the longest.
 */
int
last_match( path, entnum) char *path; int entnum; {
	int i;
	char key[ LINELEN];

        KEYCPY( key, path);
        for ( i = entnum + 1; i < entrycnt; i++) {
                switch ( keyncmp( key, i)) {
		case -1: break;		/* quit at   /a/b/d */
		case 0:  entnum = i;	/* remember  /a or /a/b */
		case 1:  continue;	/* skip over /a/a */
		}
		break; /* quit loop */
	}
	return( entnum);
}

/* compare the path r with i's sortkey in the following way:
 * if the key is a subpath of r, return 0, as a match.
 * if the key is < or > r, return 1 or -1  respectively.
 */
int
keyncmp( r, i) char *r; int i; {
	char *l;
	int diff, n;

	l = entries[ i].sortkey;
	n = entries[ i].keylen;

	diff = SIGN( strncmp( r, l, n));

	/* if diff == 0 & n != 0 ( l isn't root) &
	 * r[n] == '\0' or '\001', we have a match.
	 * if n == 0, l is root, which matches everything.
	 */
	return( diff? diff: n ? ( (unsigned) r[n] > '\001') : 0);   /* XXX */
}

struct currentness *
dec_entry( entnum, fr, to, cmp, tail)
int entnum; char *fr[], *to[], *cmp[], *tail; {
        static struct currentness currency_buf, *entry_currency;
	int i;
	Entry *e;
	static int xref_flag = 0;

	/* this routine's main purpose is to transfer entnum's contents
	 * to the paths fr, to, & cmp.
	 * for efficiency and data-hiding reasons, we maintain various static
	 * data, including the currentness data for readstat's last update.
	 */

	/* we avoid calling get_currentness(cmp) redundantly, but not just
	 * for efficiency reasons: this helps our update-simulation for
	 * nopullflag-support. see update_file().
	 * each cmpfile may get updated at most once;
	 * if it does, update_file() will mark its currency "out-of-date".
	 * xref_flag is set if any entry uses another entry's file
	 * as a cmpfile. in this case, we have to search entries[]
	 * to see if what we've modified is another entry's cmpfile.
	 * if so, propagate the "out-of-date" mark to that entry.
	 */
	if ( xref_flag && updated( &currency_buf, NULL)) {
		for ( e = &entries[ i = 1]; i <= entrycnt; e = &entries[ ++i]) {
			if ( updated( &e->currency, NULL) ||
			     strcmp(   e->cmpfile,   currency_buf.name));
			else updated( &e->currency, &currency_buf);
		}
	}
	currency_buf.sbuf.st_mode = S_IFMT;	/* kill short-term data. */

	if (    prev_ent != entnum) {
		prev_ent =  entnum;

		/* a subtler, longer search would set this flag less often.
		 */
		if ( ! writeflag)
			xref_flag = strncmp( entries[ entnum].cmpfile,
					     entries[ entnum].tofile,
				     strlen( entries[ entnum].tofile));

		poppath( fr); pushpath( fr,  entries[ entnum].fromfile);
		poppath( to); pushpath( to,  entries[ entnum].tofile);
		poppath(cmp); pushpath( cmp, entries[ entnum].cmpfile);
		
		entry_currency = &entries[ entnum].currency;
	}
	if ( updated( entry_currency, NULL))
		get_currentness( cmp, entry_currency);

	if ( ! tail || ! *tail )
		return( entry_currency);

	if ( S_IFDIR == TYPE( entry_currency->sbuf)) { /* usual case */
		pushpath( cmp, tail);
		get_currentness( cmp, &currency_buf);
		poppath( cmp);
		return( &currency_buf);
	}
	/* rarely, a directory may have a non-dir as its cmpfile.
	 * if the entry's cmpfile isn't a dir, then cmp[ ROOT],
	 * is the comparison-file for each of the tofile's dependents.
	 */
	return( entry_currency);
}

/* these routines handle a stack of pointers into a character-string,
 * which typically is a UNIX pathname.
 * the first element CNT of the stack points to a depth-counter.
 * the second element ROOT points to the beginning of the pathname string.
 * subsequent elements point to terminal substrings of the pathname.
 * a slash precedes each element.
 */
#define COUNT(p) (*(int*)p[CNT])
char **
initpath( name) char *name; {
	char **p;

	/* for each stack-element, alloc a pointer 
	 * and 15 chars for a filename:
	 */
	p =      (char **) malloc( stackmax * sizeof NULL);
	p[ CNT] = (char *) malloc( stackmax * 15 + sizeof ((int) 0));
	COUNT( p) = 1;
	p[ ROOT] = p[ CNT] + sizeof ((int) 1);
	strcpy( p[ ROOT], name);
	p[ NAME] = p[ ROOT] + strlen( name);
	return( p);
}
int
pushpath( p, name) char **p; char *name; {
	char *top;

	if ( ! p) return( -1);
	if ( ++COUNT( p) >= stackmax) {
		sprintf( errmsg, "%s\n%s\n%s %d.\n",
			"path stack overflow: directory too deep:", p[ ROOT],
			"use -S option, with value >", stackmax);
		do_panic();
	}
	if ( *name) *p[ COUNT( p)]++ = '/';
	top = p[ COUNT( p)];
	strcpy( top, name);
	p[ COUNT( p) + 1] = top + strlen( top);
	return( COUNT( p));
}
void
poppath( p) char **p; {
	if ( ! p) return;
	else if ( 1 >= COUNT( p)) {
		sprintf(errmsg,"can't pop root from path-stack");
		do_panic();
	}
	else if ( *p[ COUNT( p)]) p[ COUNT( p)]--;
		/* non-null last elt; remove its initial slash */
	*p[  COUNT( p)--] = '\0';
	return;
}
