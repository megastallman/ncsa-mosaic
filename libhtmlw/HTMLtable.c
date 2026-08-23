#include "../config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <X11/Xlib.h>
#include "HTMLP.h"
#include "HTML.h"
#include "list.h"
#ifdef VAXC
#include <ctype.h>
#endif /* VAXC, need for isspace, GEC */

#define	DEFAULT_FIELD_WIDTH	30
#define	DEFAULT_FIELD_HEIGHT	20

#define FIELD_BORDER_SPACE	5	/*aesthetic space around element */

#define FONTHEIGHT(font) (font->max_bounds.ascent + font->max_bounds.descent)

extern char *ParseMarkTag();

#ifndef DISABLE_TRACE
extern int htmlwTrace;
#endif

/* Allocate a TableField and initialize to default values
 * return 0 on failure
 */
static void TableDraw();
static char *TableAnchorAt();
TableInfo *MakeTable();
extern Pixmap InfoToImage();
extern int HTMLTextWidth();
extern void HTMLTextExtents();
extern WidgetInfo *TableMakeWidget();
extern WidgetInfo *TableMakeSelectWidget();
extern WidgetInfo *TableMakeTextAreaWidget();
extern WidgetInfo *TableMakeButtonWidget();

/* append an image or widget item to a cell's run list */
static void TableAddRunItem(field, href, image, winfo, table)
TableField *field;
char *href;
ImageInfo *image;
WidgetInfo *winfo;
TableInfo *table;
{
CellRun *lr;

	field->runs = (CellRun *)realloc(field->runs,
		(field->run_cnt + 1) * sizeof(CellRun));
	lr = &field->runs[field->run_cnt];
	lr->text = (char *) 0;
	lr->href = (href != (char *) 0) ? strdup(href) : (char *) 0;
	lr->font = (XFontStruct *) 0;
	lr->image = image;
	lr->winfo = winfo;
	lr->table = (struct table_rec *) table;
	lr->linebreak = 0;
	field->run_cnt++;
}

/* append n explicit line breaks (<br>; two for a paragraph break) */
static void TableAddBreak(field, n)
TableField *field;
int n;
{
CellRun *lr;

	while (n-- > 0) {
		field->runs = (CellRun *)realloc(field->runs,
			(field->run_cnt + 1) * sizeof(CellRun));
		lr = &field->runs[field->run_cnt];
		lr->text = (char *) 0;
		lr->href = (char *) 0;
		lr->font = (XFontStruct *) 0;
		lr->image = (ImageInfo *) 0;
		lr->winfo = (WidgetInfo *) 0;
		lr->table = (struct table_rec *) 0;
		lr->linebreak = 1;
		field->run_cnt++;
		}
}

static TableField *NewTableField()
{
TableField *tf;

	if (!(tf = (TableField *) malloc(sizeof(TableField)))) {
		return(0);
		}
	tf->alignment = ALIGN_CENTER;
	tf->valign = ALIGN_MIDDLE;
	tf->colSpan = 1;
	tf->rowSpan = 1;
	tf->contVert = False;
	tf->contHoriz = False;
	tf->maxWidth = DEFAULT_FIELD_WIDTH;
	tf->minWidth = DEFAULT_FIELD_WIDTH;
	tf->maxHeight = DEFAULT_FIELD_HEIGHT;
	tf->minHeight = DEFAULT_FIELD_HEIGHT;
	tf->header = False;

	tf->type = F_NONE;
	tf->text = (char *) 0;
	tf->href = (char *) 0;
	tf->runs = (CellRun *) 0;
	tf->run_cnt = 0;
	tf->font = (XFontStruct *) 0;
	tf->formattedText = (char **) 0;
	tf->numLines = 0;

	tf->image = (ImageInfo *) 0;

	tf->reqWidth = 0;
	tf->reqPercent = 0;
	tf->has_bg = False;
	tf->bg = (Pixel) 0;

	return(tf);
}

extern int HTMLAllocColor();


/* parse a WIDTH= attribute value: "50%" style into *pct, plain
   pixels into *px; nonsense is ignored */
static void TableParseWidth(val, px, pct)
char *val;
int *px, *pct;
{
int n;

	*px = 0;
	*pct = 0;
	if (val == (char *) 0) {
		return;
		}
	n = atoi(val);
	if (n <= 0) {
		return;
		}
	if (strchr(val, '%') != (char *) 0) {
		if (n > 100) {
			n = 100;
			}
		*pct = n;
		}
	else {
		if (n > 30000) {
			n = 30000;
			}
		*px = n;
		}
}


/* parse a VALIGN= attribute value; dflt comes back for anything
   unrecognized (baseline behaves like top here) */
static int TableParseValign(val, dflt)
char *val;
int dflt;
{
	if (val == (char *) 0) {
		return(dflt);
		}
	if (caseless_equal(val, "top")||caseless_equal(val, "baseline")) {
		return(ALIGN_TOP);
		}
	if (caseless_equal(val, "bottom")) {
		return(ALIGN_BOTTOM);
		}
	if (caseless_equal(val, "middle")||caseless_equal(val, "center")) {
		return(ALIGN_MIDDLE);
		}
	return(dflt);
}


/* return a word out of the text */
void GetWord(text,retStart,retEnd)
char *text;	 /* text to get a word out of */
char **retStart; /* RETURNED: start of word in text */
char **retEnd;	 /* RETURNED: end of word in text */
{
char *start;
char *end;

	if (!text) {
		*retStart = *retEnd = text;
		return;
		}

	start = text;
	while ((*start) && isspace(*start)){ /*skip over leading space*/
		start++;
		}

	end = start;
	while((*end) && (!isspace(*end))){ /* find next space */
		end++;
		}

	*retStart = start;
	*retEnd = end;
	return;
}



/* PourText() this routine pours a text string of a particular font into a
   rectangular area of specified dimensions.  The return value is a list of
   text lines that will fit within the given width.
   If a height is specified, then text will be truncated if necessary to fit.
   If height is 0, then all of the text is in the list.
   The actual pixel height of the text is returned in variable height.
*/
int PourText(text,font,width,height,percentVertSpace,formattedText,numberOfLines)
char *text; /* assumed that text is already clean and without newlines */
XFontStruct *font;
int width;   /* width of area to pour text */
int *height; /* if passed height value is zero, then height is returned */
	     /* if passed height is non zero, then truncate to this height */
int percentVertSpace; /* likely hw->html.percent_vert_space */
		      /* needed to compute height of text */
char ***formattedText; /* RETURNED: array of text lines */
int *numberOfLines;   /* RETURNED: number of lines */
{
int fontHeight;
int stringWidth;
int builtWidth;
char *textPtr;
char *wordStart,*wordEnd;
int wordWidth;  /* in pixels */
int wordLength; /* in chars */
List textList;  /* returned list of text lines */
char tmpBuff[5120]; /*ACK!, can't have more than 5k chars on a line in a field*/
int spaceWidth; /* width of a space in this font */
char **cTextList;
int numLines;
int y;

#ifndef DISABLE_TRACE
	if (htmlwTrace) {
		fprintf(stderr,"PourText: \"%s\" width=%d,height=%d\n",text,width,height);
	}
#endif

	if (!text) {
		*height = 0;
		*formattedText = (char **) 0;
		*numberOfLines = 0;
		return(0);
		}

	/* A squeezed table can hand us a sliver (or even a negative
	   width); never wrap narrower than a couple of characters. */
	if (width < 16) {
		width = 16;
		}

	textList = ListCreate();
        stringWidth = HTMLTextWidth(font,text,strlen(text));
	if (stringWidth < width) {
		ListAddEntry(textList,strdup(text));
		}
	else {

	    builtWidth = 0;
	    textPtr = text;
	    spaceWidth = HTMLTextWidth(font," ",1);
	    *tmpBuff = '\0';
	    while (*textPtr) {

#ifndef DISABLE_TRACE
		if (htmlwTrace) {
			printf("textPtr = \"%s\"\n",textPtr);
		}
#endif

		GetWord(textPtr,&wordStart,&wordEnd);
		wordLength = (int) (wordEnd - wordStart);
		wordWidth = HTMLTextWidth(font,wordStart, wordLength);
		if ((builtWidth + spaceWidth + wordWidth)  < width) {
						/* then add to line */
			if (builtWidth) {
				/* only add space if something on line already*/
				strcat(tmpBuff, " ");
				builtWidth += spaceWidth;
				}
			strncat(tmpBuff, wordStart, wordLength);
			builtWidth += wordWidth;
			}
		else if (wordWidth < width) {
			/* start new line */
			ListAddEntry(textList, strdup(tmpBuff));
			*tmpBuff = '\0';
			builtWidth = 0;

			/* and add it to the line */
			strncat(tmpBuff, wordStart, wordLength);
			builtWidth += wordWidth;
			}
		else {
			/* word is too big to fit on a line */
			/* so break up word */

			/* start new line */
			ListAddEntry(textList, strdup(tmpBuff));
			*tmpBuff = '\0';
			builtWidth = 0;

			/* find the max that will fit on a line*/
			wordWidth = 0;
			wordLength = 0;
			wordEnd = wordStart;
			while ((*wordEnd) && (width > wordWidth)) {
				wordEnd++;
				wordLength = (int) (wordEnd - wordStart);
				wordWidth = HTMLTextWidth(font,wordStart,
							wordLength);
				}

			/* the field may be narrower than one character;
			   take one anyway or we would loop here forever,
			   eating memory until the OOM killer steps in */
			if (wordEnd == wordStart) {
				wordEnd++;
				wordLength = 1;
				}

			strncat(tmpBuff, wordStart, wordLength);
			builtWidth += wordWidth;

			}
		textPtr = wordEnd;
		}

	    if (*tmpBuff) {
		ListAddEntry(textList, strdup(tmpBuff));
		}
	    }

	/* ok, we haven't paid attention to height, so now we are going
	   to remove placed lines that don't fit */

	fontHeight = FONTHEIGHT(font);
	fontHeight += (fontHeight * (percentVertSpace/100));
	if (*height) {
		/* truncate list to specified height */
		while ((ListCount(textList) * fontHeight) > (*height)) {
			ListDeleteEntry(textList, ListTail(textList));
			}
		*height = ListCount(textList) * fontHeight;
		}
	else {
		/* return the height */
		*height = ListCount(textList) * fontHeight;
		}

	/* turn text list into an array */
	numLines = ListCount(textList);
	if (!(cTextList = (char **) malloc(sizeof(char *)* numLines))) {
#ifndef DISABLE_TRACE
		if (htmlwTrace) {
			fprintf(stderr,"Out of Memory\n");
		}
#endif
		*formattedText = (char **) 0;
		*numberOfLines = 0;
		return(0);
		}
	for (y = 0; y < numLines; y++) {
		cTextList[y] = ListHead(textList);
		(void) ListDeleteEntry(textList,cTextList[y]);
		}
	ListDestroy(textList);
	*formattedText = cTextList;
	*numberOfLines = numLines;

	return(numLines);

} /* PourText() */


/* print out the table to stdout */
TableDump(t)
TableInfo *t;
{
register int x,y;

#ifndef DISABLE_TRACE
	if (htmlwTrace) {

	fprintf(stderr,"Table dump:\n");
	fprintf(stderr,"Border width is %d\n",t->borders);
	fprintf(stderr,"numColumns=%d, numRows=%d\n",t->numColumns,t->numRows);
	fprintf(stderr,"---------------------------------------------------\n");
	for (y = 0; y < t->numRows; y++ ) {
		fprintf(stderr,"|");
		for (x = 0; x < t->numColumns; x++ ) {
			fprintf(stderr,"colWidth=%d,rowHeight=%d | ",
				t->table[y * t->numColumns + x].colWidth,
				t->table[y * t->numColumns + x].rowHeight);
			}
		fprintf(stderr,"\n---------------------------------------------------\n");
		}

	}
#endif
}


/* fill out uneven rows in table */
/* return 0 on out of memory, 1 on suceess */
static int TableCleanUp(t,tableList)
TableInfo *t;
List tableList;
{
int maxNumCols;
int numColsInRow;
List rowList;
TableField *field;
int x,y;

	/* determine number of columns all rows should have */
	rowList = (List) ListHead(tableList);
	t->numRows = 0;
	maxNumCols = 0;
	while(rowList) {
		field = (TableField *) ListHead(rowList);
		numColsInRow = 0;
		while (field) {
			numColsInRow++;
			field = (TableField *) ListNext(rowList);
			}
		maxNumCols=(maxNumCols > numColsInRow)?maxNumCols:numColsInRow;
		t->numRows++;
		rowList = (List) ListNext(tableList);
		}

	t->numColumns = maxNumCols;


	/* make all rows have correct number of columns */
	rowList = (List) ListHead(tableList);
	while(rowList) {
		numColsInRow = 0;
		field = (TableField *) ListHead(rowList);
		numColsInRow = 0;
		while (field) {
			numColsInRow++;
			field = (TableField *) ListNext(rowList);
			}

		while(numColsInRow < t->numColumns) {
			/* fill up the table with empty fields */
			if (!(field = NewTableField())) {
				return(0); /* out of memory */
				}
			ListAddEntry(rowList,field);
			numColsInRow++;
			}
		rowList = (List) ListNext(tableList);
		}


	/* move 2D link list table to an array for speed */
	if (!(t->table = (TableField *) malloc(sizeof(TableField)
				* t->numColumns * t->numRows))) {
		return(0); /* out of memory */
		}
	y=0;
	rowList = (List) ListHead(tableList);
	while(rowList) {
		x = 0;
		field = (TableField *) ListHead(rowList);
		while (field) {
			memcpy(&(t->table[y * t->numColumns + x]), field,
						sizeof(TableField));
			x++;
			field = (TableField *) ListNext(rowList);
			}
		y++;
		rowList = (List) ListNext(tableList);
		}


	return(1);

}

/* return the number of connected fields */
static int TableHowManyConnectedHorizFields(t,xpos,ypos)
TableInfo *t;
int xpos,ypos;
{
int count;
register int x;

	count = 0;
	for (x = xpos+1; x < t->numColumns; x++) {
		if (t->table[ypos * t->numColumns + x].contHoriz) {
			count++;
			}
		else {
			return(count);
			}
		}
	return(count);
}

/* return the number of connected fields */
static int TableHowManyConnectedVertFields(t,xpos,ypos)
TableInfo *t;
int xpos,ypos;
{
int count;
register int y;

	count = 0;
	for (y = ypos+1; y < t->numRows; y++) {
		if (t->table[y * t->numColumns + xpos].contVert) {
			count++;
			}
		else {
			return(count);
			}
		}
	return(count);
}

static int CalculateMaxWidthOfColumn(t,x)
TableInfo *t;
int x;
{
register int y;
register int maxWidth=0;
register int width;

	for (y=0; y < t->numRows; y++) {
		width = t->table[y * t->numColumns+x].maxWidth;
		maxWidth = (maxWidth > width) ? maxWidth : width;
		}
	return(maxWidth);
}

/*
 * The narrowest a table can be poured to: every column squeezed to
 * its longest-word floor.  This is what a nested table contributes
 * as its minimum in the enclosing cell -- its CURRENT width is just
 * how it happened to be poured (at full page width at build time)
 * and would inflate the parent's floors until the page overflows.
 */
static int TableMinContentWidth(t)
TableInfo *t;
{
int x, y, colmin, w;

	if (t == (TableInfo *) 0) {
		return(0);
		}
	w = 2 * t->borders + t->cellspacing;
	for (x = 0; x < t->numColumns; x++) {
		colmin = 0;
		for (y = 0; y < t->numRows; y++) {
			if (t->table[y * t->numColumns + x].minWidth >
			    colmin) {
				colmin = t->table[y * t->numColumns +
					x].minWidth;
				}
			}
		w += colmin + 2 * t->cellpadding + t->cellspacing;
		}
	return(w);
}

/*
 * Flow a text cell's runs into `width` pixels, greedy word-wrapped.
 * One deterministic layout serves three callers, so what is measured
 * is exactly what is drawn and exactly what is hit-tested:
 *   CELLFLOW_MEASURE: return the flowed height in *retheight.
 *   CELLFLOW_DRAW:    draw the words at x,y (view coords), linked
 *                     runs in the anchor color with an underline,
 *                     the block vertically centered in `height`.
 *   CELLFLOW_HIT:     return the href of the run under ex,ey.
 */

#define CELLFLOW_MEASURE	0
#define CELLFLOW_DRAW		1
#define CELLFLOW_HIT		2

struct cell_word {
	char *p;
	int len;
	int width;
	int run;
	int brk;	/* explicit <br>s directly before this word */
	int ln;		/* line this word landed on */
	int lx;		/* x offset within its line */
};

static char *TableCellFlow(hw, eptr, field, x, y, width, height,
			mode, ex, ey, retheight, retwidth, retminword)
HTMLWidget hw;
struct ele_rec *eptr;
TableField *field;
int x, y;
int width, height;
int mode;
int ex, ey;
int *retheight;
int *retwidth;		/* widest flowed line (or NULL) */
int *retminword;	/* widest single word/item (or NULL) */
{
struct cell_word *words;
int nwords, wcap;
int *linew, *lineasc, *linedesc, *liney;
int i, j, r;
int cx, line, nlines, totalh, starty, maxlinew;
char *result;
XFontStruct *rfont;
CellRun *run;

	if (retheight != (int *) 0) {
		*retheight = 0;
		}
	if (retwidth != (int *) 0) {
		*retwidth = 0;
		}
	if (retminword != (int *) 0) {
		*retminword = 0;
		}
	if ((field->run_cnt <= 0)||(field->font == (XFontStruct *) 0)) {
		return((char *) 0);
		}
	if (width < 16) {
		width = 16;
		}

	/* split text runs into words; an image or widget item is one
	   pseudo-word of its own size; a <br> run marks the next word */
	words = (struct cell_word *) 0;
	nwords = 0;
	wcap = 0;
	{
	int pendbrk = 0;

	for (r = 0; r < field->run_cnt; r++) {
		run = &field->runs[r];
		if (run->linebreak) {
			pendbrk++;
			}
		else if (run->text != (char *) 0) {
			char *p = run->text;
			char *ws, *we;

			rfont = (run->font != (XFontStruct *) 0) ?
				run->font : field->font;
			while (*p != '\0') {
				GetWord(p, &ws, &we);
				if (we == ws) {
					break;
					}
				if (nwords >= wcap) {
					wcap = wcap ? wcap * 2 : 32;
					words = (struct cell_word *)realloc(
					    (char *)words,
					    wcap * sizeof(struct cell_word));
					}
				words[nwords].p = ws;
				words[nwords].len = (int)(we - ws);
				words[nwords].width = HTMLTextWidth(rfont,
					ws, words[nwords].len);
				words[nwords].run = r;
				words[nwords].brk = pendbrk;
				pendbrk = 0;
				nwords++;
				p = we;
				}
			}
		else if ((run->image != (ImageInfo *) 0)||
			 (run->winfo != (WidgetInfo *) 0)||
			 (run->table != (struct table_rec *) 0)) {
			if (nwords >= wcap) {
				wcap = wcap ? wcap * 2 : 32;
				words = (struct cell_word *)realloc(
				    (char *)words,
				    wcap * sizeof(struct cell_word));
				}
			words[nwords].p = (char *) 0;
			words[nwords].len = 0;
			if (run->image != (ImageInfo *) 0) {
				words[nwords].width = run->image->width;
				}
			else if (run->winfo != (WidgetInfo *) 0) {
				words[nwords].width = run->winfo->width;
				}
			else {
				words[nwords].width =
					((TableInfo *)run->table)->width;
				}
			words[nwords].run = r;
			words[nwords].brk = pendbrk;
			pendbrk = 0;
			nwords++;
			}
		}
	}
	if (nwords == 0) {
		if (words != (struct cell_word *) 0) {
			free((char *)words);
			}
		return((char *) 0);
		}

	/* greedy line breaking; inter-word gaps use the incoming
	   word's font so joined segments measure exactly.  A nested
	   table is block-level: it always takes a line of its own, or
	   sibling tables (opennet comment threads) sit side by side */
	cx = 0;
	line = 0;
	maxlinew = 0;
	for (i = 0; i < nwords; i++) {
		int sp;
		int blocky;

		run = &field->runs[words[i].run];
		blocky = (run->table != (struct table_rec *) 0);
		if ((!blocky)&&(i > 0)&&
		    (field->runs[words[i - 1].run].table !=
		     (struct table_rec *) 0)) {
			blocky = 1;
			}
		/* explicit <br>s: end the current line; each extra one
		   leaves a blank line behind */
		if (words[i].brk > 0) {
			line += words[i].brk;
			cx = 0;
			}
		rfont = (run->font != (XFontStruct *) 0) ?
			run->font : field->font;
		sp = HTMLTextWidth(rfont, " ", 1);
		if ((cx > 0)&&
		    (blocky ||
		     ((cx + sp + words[i].width) > width))) {
			line++;
			cx = 0;
			}
		words[i].ln = line;
		words[i].lx = cx ? (cx + sp) : 0;
		cx = words[i].lx + words[i].width;
		if (cx > maxlinew) {
			maxlinew = cx;
			}
		if (retminword != (int *) 0) {
			int mw;

			/* a table item can be poured narrower than it
			   currently is, down to its min-content width;
			   text words and other items are unsplittable */
			mw = words[i].width;
			if (run->table != (struct table_rec *) 0) {
				mw = TableMinContentWidth(
					(TableInfo *)run->table);
				if (mw > words[i].width) {
					mw = words[i].width;
					}
				}
			if (mw > *retminword) {
				*retminword = mw;
				}
			}
		}
	nlines = line + 1;

	/* per-line metrics: text words contribute their font's ascent
	   and descent, items sit with their bottom on the baseline */
	lineasc = (int *)malloc(nlines * sizeof(int));
	linedesc = (int *)malloc(nlines * sizeof(int));
	liney = (int *)malloc(nlines * sizeof(int));
	linew = (int *)malloc(nlines * sizeof(int));
	for (i = 0; i < nlines; i++) {
		lineasc[i] = 0;
		linedesc[i] = 0;
		linew[i] = 0;
		}
	for (i = 0; i < nwords; i++) {
		int ln = words[i].ln;

		run = &field->runs[words[i].run];
		if (run->text != (char *) 0) {
			rfont = (run->font != (XFontStruct *) 0) ?
				run->font : field->font;
			if (rfont->max_bounds.ascent > lineasc[ln]) {
				lineasc[ln] = rfont->max_bounds.ascent;
				}
			if (rfont->max_bounds.descent > linedesc[ln]) {
				linedesc[ln] = rfont->max_bounds.descent;
				}
			}
		else {
			int ih;

			if (run->image != (ImageInfo *) 0) {
				ih = run->image->height;
				}
			else if (run->winfo != (WidgetInfo *) 0) {
				ih = run->winfo->height;
				}
			else {
				ih = ((TableInfo *)run->table)->height;
				}
			if (ih > lineasc[ln]) {
				lineasc[ln] = ih;
				}
			}
		if ((words[i].lx + words[i].width) > linew[ln]) {
			linew[ln] = words[i].lx + words[i].width;
			}
		}
	/* a line no word landed on (consecutive <br>s) is blank but
	   still one text line tall */
	for (i = 0; i < nlines; i++) {
		if ((lineasc[i] == 0)&&(linedesc[i] == 0)) {
			lineasc[i] = field->font->max_bounds.ascent;
			linedesc[i] = field->font->max_bounds.descent;
			}
		}
	totalh = 0;
	for (i = 0; i < nlines; i++) {
		liney[i] = totalh;
		totalh += lineasc[i] + linedesc[i];
		}

	if (retheight != (int *) 0) {
		*retheight = totalh;
		}
	if (retwidth != (int *) 0) {
		*retwidth = maxlinew;
		}
	if (mode == CELLFLOW_MEASURE) {
		free((char *)words);
		free((char *)lineasc);
		free((char *)linedesc);
		free((char *)liney);
		free((char *)linew);
		return((char *) 0);
		}

	/* the cell's VALIGN places the flowed block in the cell */
	if (field->valign == ALIGN_TOP) {
		starty = y;
		}
	else if (field->valign == ALIGN_BOTTOM) {
		starty = y + height - totalh;
		}
	else {
		starty = y + (height - totalh) / 2;
		}
	if (starty < y) {
		starty = y;
		}

	/* walk segments: consecutive text words on one line in one run;
	   an item is a segment of its own */
	result = (char *) 0;
	for (i = 0; i < nwords; ) {
		int sx, sy, sw, off, ln;

		run = &field->runs[words[i].run];
		ln = words[i].ln;
		j = i;
		if (run->text != (char *) 0) {
			while ((j < nwords)&&(words[j].ln == ln)&&
				(words[j].run == words[i].run)) {
				j++;
				}
			}
		else {
			j = i + 1;
			}

		if (field->alignment == ALIGN_CENTER) {
			off = (width - linew[ln]) / 2;
			}
		else if (field->alignment == ALIGN_RIGHT) {
			off = width - linew[ln];
			}
		else {
			off = 0;
			}
		if (off < 0) {
			off = 0;
			}
		sx = x + off + words[i].lx;
		sy = starty + liney[ln];
		sw = words[j-1].lx + words[j-1].width - words[i].lx;

		if (mode == CELLFLOW_HIT) {
			if ((run->winfo == (WidgetInfo *) 0)&&
			    (ex >= sx)&&(ex < (sx + sw))&&
			    (ey >= sy)&&
			    (ey < (sy + lineasc[ln] + linedesc[ln]))) {
				if (run->table != (struct table_rec *) 0) {
					/* into the nested table's grid */
					result = TableAnchorAt(
						(TableInfo *)run->table,
						sx,
						sy + lineasc[ln] -
						((TableInfo *)run->table)->height,
						ex, ey);
					}
				else {
					result = run->href;
					}
				break;
				}
			}
		else if (run->table != (struct table_rec *) 0) {
			/* a nested table item: draw its grid with its
			   bottom on the line's baseline */
			TableDraw(hw, eptr, (TableInfo *)run->table,
				sx,
				sy + lineasc[ln] -
					((TableInfo *)run->table)->height);
			}
		else if (run->image != (ImageInfo *) 0) {
			ImageInfo *pic = run->image;

			if ((pic->image == None)&&
			    (pic->image_data != NULL)) {
				pic->image = InfoToImage(hw, pic, 0);
				}
			if (pic->image != None) {
				XCopyArea(XtDisplay(hw), pic->image,
					XtWindow(hw->html.view),
					hw->html.drawGC,
					0, 0, pic->width, pic->height,
					sx,
					sy + lineasc[ln] - pic->height);
				}
			}
		else if (run->winfo != (WidgetInfo *) 0) {
			WidgetInfo *wp = run->winfo;
			int wty;

			if (wp->w != NULL) {
				wty = sy + lineasc[ln] - wp->height;
				/* doc coordinates keep ScrollWidgets
				   honest when the page scrolls */
				wp->x = sx + hw->html.scroll_x;
				wp->y = wty + hw->html.scroll_y;
				XtMoveWidget(wp->w, sx, wty);
				wp->seeable = 1;
				if (wp->mapped == False) {
					wp->mapped = True;
					XtSetMappedWhenManaged(wp->w, True);
					}
				}
			}
		else {
			/* join the segment's words with single spaces:
			   core font widths are additive, so the joined
			   string measures exactly the layout */
			char *seg;
			int sl, k;
			XmString ttd;
			XmFontList tftd;

			sl = 0;
			for (k = i; k < j; k++) {
				sl += words[k].len + 1;
				}
			seg = (char *)malloc(sl + 1);
			sl = 0;
			for (k = i; k < j; k++) {
				if (k > i) {
					seg[sl++] = ' ';
					}
				memcpy(seg + sl, words[k].p, words[k].len);
				sl += words[k].len;
				}
			seg[sl] = '\0';

			rfont = (run->font != (XFontStruct *) 0) ?
				run->font : field->font;
			XSetForeground(XtDisplay(hw), hw->html.drawGC,
				(run->href != (char *) 0) ?
				hw->html.anchor_fg : eptr->fg);
			XSetBackground(XtDisplay(hw), hw->html.drawGC,
				eptr->bg);
			XSetFont(XtDisplay(hw), hw->html.drawGC,
				rfont->fid);
			ttd = XmStringCreateLocalized(seg);
			tftd = XmFontListCreate(rfont,
				XmSTRING_DEFAULT_CHARSET);
			XmStringDraw(XtDisplay(hw),
				XtWindow(hw->html.view),
				tftd, ttd, hw->html.drawGC,
				sx,
				/* top y such that mixed fonts share
				   the line's baseline */
				sy + (lineasc[ln] -
					rfont->max_bounds.ascent),
				XmStringWidth(tftd, ttd),
				XmALIGNMENT_BEGINNING,
				XmSTRING_DIRECTION_L_TO_R, NULL);
			XmStringFree(ttd);
			XmFontListFree(tftd);
			if (run->href != (char *) 0) {
				XDrawLine(XtDisplay(hw),
					XtWindow(hw->html.view),
					hw->html.drawGC,
					sx, sy + lineasc[ln] + 1,
					sx + sw, sy + lineasc[ln] + 1);
				}
			free(seg);
			}

		i = j;
		}

	free((char *)lineasc);
	free((char *)linedesc);
	free((char *)liney);
	free((char *)linew);
	free((char *)words);
	return(result);
}


TableCalculateDimensions(hw,t,pageWidth)
HTMLWidget hw;
TableInfo *t;
int pageWidth;		/* width in pixels of output page */
{
TableField *field;
register int xx,x,y;
int sumMaxWidth;	/* summation of max widths */
int maxWidthOfColumn;
int maxHeightOfRow;
int sumMinWidth;	/* summation of max widths */
int maxWidthOfRow;
int minWidthOfRow;
int numAdjacent;
float percentToShrink;
int accumulateColWidth;
int hasreq;


	/* honor the table's own WIDTH= attribute as its target width;
	   percent is of the space we were given, and a pixel request
	   never grows the target past that space */
	if (t->reqPercent > 0) {
		pageWidth = (pageWidth * t->reqPercent) / 100;
		}
	else if ((t->reqWidth > 0)&&(t->reqWidth < pageWidth)) {
		pageWidth = t->reqWidth;
		}
	if (pageWidth < 16) {
		pageWidth = 16;
		}

	/* cell WIDTH= requests are honored by the distribution branch */
	hasreq = 0;
	for (y = 0; (y < t->numRows)&&(!hasreq); y++) {
		for (x = 0; x < t->numColumns; x++) {
			field = &(t->table[y * t->numColumns + x]);
			if ((field->reqWidth > 0)||(field->reqPercent > 0)) {
				hasreq = 1;
				break;
				}
			}
		}

	/* calculate max and min width for each field*/
	sumMaxWidth = 0;
	sumMinWidth = 0;
	for (y = 0; y < t->numRows; y++) {
	    maxWidthOfRow = 0;
	    minWidthOfRow = 0;
	    for (x = 0; x < t->numColumns; x++ ) {

		field = &(t->table[y * t->numColumns + x]);
		field->minWidth = 0;
		if (field->type == F_TEXT) {
			int th, tw, tm;

			/* the flow at unlimited width gives the one-line
			   width (max), the widest single word (min, the
			   narrowest the cell can wrap to) and the line
			   height, all with per-run fonts */
			TableCellFlow(hw, (struct ele_rec *) 0, field,
				0, 0, (1 << 28), 0, CELLFLOW_MEASURE,
				0, 0, &th, &tw, &tm);
			field->maxWidth = tw;
			field->minWidth = tm;
			field->minHeight = th;
			}
		else {
			/* non text */
			field->maxWidth = 0;
			}
		maxWidthOfRow += field->maxWidth;

		field->maxHeight = 0;
		minWidthOfRow += field->minWidth;
		}
	    /* save the length of the longest and shortest row */
	    sumMaxWidth = (sumMaxWidth > maxWidthOfRow) ?
						sumMaxWidth : maxWidthOfRow;
	    sumMinWidth = (sumMinWidth > minWidthOfRow) ?
						sumMinWidth : minWidthOfRow;
	    }


	/* add padding and inter-cell spacing to widths */
	sumMaxWidth += (t->numColumns * 2 * t->cellpadding) +
		((t->numColumns + 1) * t->cellspacing);
	sumMinWidth += (t->numColumns * 2 * t->cellpadding) +
		((t->numColumns + 1) * t->cellspacing);


	/* divy up max width with adjacent continue Horizontal fields.
	   Compute the share first: the loop writes the anchor before
	   the continuations, and it must cover ALL span columns --
	   stopping one short left the last column with no share and
	   lost that slice of the span's width entirely */
	for (y = 0; y < t->numRows; y++) {
		    for (x = 0; x < t->numColumns; x++) {
			numAdjacent = TableHowManyConnectedHorizFields(t,x,y);
			if (numAdjacent) {
			    int xx, share;

			    share = t->table[y*t->numColumns+x].maxWidth
						/ (numAdjacent+ 1);
			    for (xx = x; xx < x + numAdjacent + 1; xx++) {
				t->table[y * t->numColumns+xx].maxWidth
					= share;
				}
			    }
			x += numAdjacent;
			}
		    }
	/* divy up min height with adjacent continue Vertical fields */
	for (x = 0; x < t->numColumns; x++) {
		    for (y = 0; y < t->numRows; y++) {
			numAdjacent = TableHowManyConnectedVertFields(t,x,y);
			if (numAdjacent) {
			    int yy, share;

			    share = t->table[y*t->numColumns+x].minHeight
						/ (numAdjacent + 1);
			    for (yy = y; yy < y + numAdjacent + 1; yy++) {
				t->table[yy * t->numColumns+x].minHeight
					= share;
				}
			    }
			y += numAdjacent;
			}
		    }



	/* fit table to page; cell width requests always go through the
	   distribution branch, which is what honors them */
	if ((sumMaxWidth < pageWidth)&&(!hasreq)) {
		/* fits on the page, set all fields to use max width */

		for (x = 0; x < t->numColumns; x++) {
			/* find widest field in column */
			maxWidthOfColumn = 0;
			for (y = 0; y < t->numRows; y++ ) {
			    maxWidthOfColumn =
				(maxWidthOfColumn >
				t->table[y * t->numColumns + x].maxWidth)?
					maxWidthOfColumn :
					t->table[y * t->numColumns+x].maxWidth;
			    }
			/* assign uniform width to column */
			for (y = 0; y < t->numRows; y++) {
			        t->table[y*t->numColumns + x].colWidth
						= maxWidthOfColumn
						+ 2 * t->cellpadding;
				}
			}
		for (y=0; y < t->numRows; y++) {
			/* find highest of minimum heights */
			maxHeightOfRow = 0;
			for (x=0; x < t->numColumns; x++) {
				maxHeightOfRow =
				    (maxHeightOfRow >
				    t->table[y * t->numColumns + x].minHeight)?
					maxHeightOfRow:
					t->table[y * t->numColumns+x].minHeight;
				}
			/* assign uniform height to row */
			for (x=0; x < t->numColumns; x++) {
				t->table[y * t->numColumns + x].rowHeight
						= maxHeightOfRow
						+ 2 * t->cellpadding;
				}
			}

		/* (cell text is flowed from its runs at draw time now;
		   no pre-formatted line array to build) */
		}
	else {
	/* will have to squeeze fields downward to fit on page */

		percentToShrink = ((float)pageWidth)/((float)sumMaxWidth);

		/* settle the column widths COMPLETELY before measuring
		   any height: widths still grow in the uniform-column
		   pass below, and a cell measured at a narrower width
		   than it is drawn at wraps to more lines than the
		   draw, leaving dead space at the bottom of the table.

		   Standard auto-layout distribution: every column starts
		   at its floor (longest word, or a nested table's
		   min-content, plus the padding the draw indents by) and
		   the page width left over is dealt out in proportion to
		   each column's DEFICIT -- how far below its natural
		   width it sits -- never past natural.  The floors only
		   overflow the page when they alone do, and any pixels
		   one column cannot use flow to the next tightest.  (The
		   naive shrink-then-floor-each-column-alone pushed the
		   total past the page by the sum of the floors that bit:
		   opennet's front-page headlines poked past the right
		   edge.) */
		{
			int *cmax, *cmin, *cw;
			int surplus, deficit, give, w, progressed;
			int creq;

			cmax = (int *)malloc(t->numColumns * sizeof(int));
			cmin = (int *)malloc(t->numColumns * sizeof(int));
			cw = (int *)malloc(t->numColumns * sizeof(int));
			for (x = 0; x < t->numColumns; x++) {
				cmax[x] = CalculateMaxWidthOfColumn(t,x) +
					2 * t->cellpadding;
				cmin[x] = 2 * t->cellpadding;
				creq = 0;
				for (y = 0; y < t->numRows; y++) {
					field = &(t->table[y*t->numColumns+x]);
					w = field->minWidth
						+ 2 * t->cellpadding;
					if (w > cmin[x]) {
						cmin[x] = w;
						}
					w = field->reqWidth;
					if (field->reqPercent > 0) {
						w = (pageWidth *
						     field->reqPercent) / 100;
						}
					/* a spanning cell's request is
					   split across its columns */
					if ((w > 0)&&(field->colSpan > 1)) {
						w /= field->colSpan;
						}
					if (w > creq) {
						creq = w;
						}
					}
				/* a WIDTH= request replaces the column's
				   natural width as the distribution target:
				   it caps a wide column and grows a narrow
				   one, but the longest-word floor still
				   wins over a request that is too small */
				if (creq > 0) {
					cmax[x] = creq;
					}
				if (cmax[x] < cmin[x]) {
					cmax[x] = cmin[x];
					}
				cw[x] = cmin[x];
				}
			/* the spacing between cells and the outer border
			   come out of the page before the cells share it */
			surplus = pageWidth -
				((t->numColumns + 1) * t->cellspacing) -
				(2 * t->borders);
			for (x = 0; x < t->numColumns; x++) {
				surplus -= cmin[x];
				}
			while (surplus > 0) {
				deficit = 0;
				for (x = 0; x < t->numColumns; x++) {
					deficit += cmax[x] - cw[x];
					}
				if (deficit <= 0) {
					break;
					}
				progressed = 0;
				for (x = 0; (x < t->numColumns)&&
					    (surplus > 0); x++) {
					if (cw[x] >= cmax[x]) {
						continue;
						}
					give = (int)((float)surplus *
						(float)(cmax[x] - cw[x]) /
						(float)deficit);
					if (give < 1) {
						give = 1;
						}
					if (give > (cmax[x] - cw[x])) {
						give = cmax[x] - cw[x];
						}
					if (give > surplus) {
						give = surplus;
						}
					cw[x] += give;
					surplus -= give;
					progressed = 1;
					}
				if (!progressed) {
					break;
					}
				}

			for (x = 0; x < t->numColumns; x++) {
				for (y = 0; y < t->numRows; y++) {
					field = &(t->table[y*t->numColumns+x]);
					field->colWidth = cw[x];
					field->rowHeight = 0;
					}
				}
			free((char *)cmax);
			free((char *)cmin);
			free((char *)cw);
		}

		/* divy up width with adjacent continue Horizontal fields
		   (share computed first, all span columns covered) */
		for (y = 0; y < t->numRows; y++) {
		    for (x = 0; x < t->numColumns; x++) {
			numAdjacent = TableHowManyConnectedHorizFields(t,x,y);
			if (numAdjacent) {
			    int xx, share;

			    share = t->table[y*t->numColumns+x].colWidth
						/ (numAdjacent+ 1);
			    for (xx = x; xx < x + numAdjacent + 1; xx++) {
				t->table[y * t->numColumns+xx].colWidth
					= share;
				}
			    }
			x += numAdjacent;
			}
		    }

		/* make sure all widths in a column are the same size */
		for (x = 0; x < t->numColumns; x++) {
		    maxWidthOfColumn = 0;
		    /* find biggest Width for this column */
		    for (y = 0; y < t->numRows; y++) {
			maxWidthOfColumn = (maxWidthOfColumn >
				t->table[y*t->numColumns+x].colWidth)?
				maxWidthOfColumn:
				t->table[y*t->numColumns+x].colWidth;
			}
		    /* make sure they are all the same */
		    for (y = 0; y < t->numRows; y++) {
			t->table[y*t->numColumns+x].colWidth = maxWidthOfColumn;
			}
		    }

		/* column widths are final: pour any nested table that is
		   wider than the cell it sits in again, at the cell's
		   real width (every table is first built at full page
		   width; the cell's minWidth only promised the nested
		   table's min-content width).  Recursion through this
		   same function re-pours deeper tables in turn. */
		for (x = 0; x < t->numColumns; x++) {
			for (y = 0; y < t->numRows; y++) {
				int avail, r;

				field = &(t->table[y*t->numColumns+x]);
				if ((field->type != F_TEXT)||
				    (field->run_cnt <= 0)) {
					continue;
					}
				numAdjacent = TableHowManyConnectedHorizFields
									(t,x,y);
				avail = field->colWidth;
				for (xx = x+1; xx < x+numAdjacent+1; xx++) {
				    avail += t->table[y*t->numColumns+xx].colWidth
					+ t->cellspacing;
				    }
				avail -= 2 * t->cellpadding;
				if (avail <= 0) {
					continue;
					}
				for (r = 0; r < field->run_cnt; r++) {
					TableInfo *nt;

					nt = (TableInfo *)field->runs[r].table;
					if ((nt != (TableInfo *) 0)&&
					    (nt->width > avail)) {
						TableCalculateDimensions(hw,
							nt, avail);
						}
					}
				}
			}

		/* now flow every cell at the exact width the draw will
		   use (the span's colWidths minus the border padding)
		   to get its height */
		for (x = 0; x < t->numColumns; x++) {
			for (y = 0; y < t->numRows; y++) {
				field = &(t->table[y*t->numColumns+x]);
				numAdjacent = TableHowManyConnectedHorizFields
									(t,x,y);
				accumulateColWidth = field->colWidth;
				for (xx = x+1; xx < x+numAdjacent+1; xx++) {
				    accumulateColWidth +=
					t->table[y*t->numColumns+xx].colWidth
					+ t->cellspacing;
				    }

				if (field->type == F_TEXT) {
					int th;

					TableCellFlow(hw,
						(struct ele_rec *) 0,
						field, 0, 0,
						accumulateColWidth -
							2 * t->cellpadding,
						0, CELLFLOW_MEASURE,
						0, 0, &th,
						(int *) 0, (int *) 0);
					field->rowHeight = th +
						2 * t->cellpadding;
					}

#ifndef DISABLE_TRACE
				if (htmlwTrace) {
					fprintf(stderr,"poured field %d,%d is dims %d,%d: %%shrink=%f\n",
						x,y,field->colWidth,field->rowHeight,percentToShrink);
				}
#endif

				}
			}

		/* divy up height with adjacent continue Vertical fields
		   (share computed first, all span rows covered) */
		for (x = 0; x < t->numColumns; x++) {
		    for (y = 0; y < t->numRows; y++) {
			numAdjacent = TableHowManyConnectedVertFields(t,x,y);
			if (numAdjacent) {
			    int yy, share;

			    share = t->table[y*t->numColumns+x].rowHeight
						/ (numAdjacent + 1);
			    for (yy = y; yy < y + numAdjacent + 1; yy++) {
				t->table[yy * t->numColumns+x].rowHeight
					= share;
				}
			    }
			y += numAdjacent;
			}
		    }
		/* assign uniform height to row*/
		for (y = 0; y < t->numRows; y++) {
			/* find max height of this row */
			maxHeightOfRow = 0;
			for (x = 0; x < t->numColumns; x++) {
				maxHeightOfRow = (maxHeightOfRow >
					t->table[y*t->numColumns+x].rowHeight) ?
					maxHeightOfRow :
					t->table[y*t->numColumns+x].rowHeight;
				}
			/* assign height */
			for (x = 0; x < t->numColumns; x++) {
				t->table[y * t->numColumns + x].rowHeight =
					maxHeightOfRow;
				}
			}

		}


	/* calculate table width */
	t->width = 0;
	for (x = 0; x < t->numColumns; x++) {
#ifndef DISABLE_TRACE
		if (htmlwTrace) {
			fprintf(stderr,"colWidth for %d,0 = %d\n",x,t->table[x].colWidth);
		}
#endif
		t->width += t->table[x].colWidth;
		}
	/* calculate table height */
	t->height = 0;
	for (y = 0; y < t->numRows; y++) {
#ifndef DISABLE_TRACE
		if (htmlwTrace) {
			fprintf(stderr,"rowHeight for 0,%d = %d\n",y,t->table[y * t->numColumns].rowHeight);
		}
#endif
		t->height += t->table[y * t->numColumns].rowHeight;
		}
	/* (these used to sit inside the DISABLE_TRACE conditional,
	   which would have dropped the border padding -- and the
	   caption room -- from no-trace builds) */
	t->width += (t->borders*2) +
		((t->numColumns + 1) * t->cellspacing);
	t->height += (t->borders*2) +
		((t->numRows + 1) * t->cellspacing);

	/* leave room to draw the caption */
	t->captionHeight = 0;
	if ((t->caption != NULL)&&(t->caption[0] != '\0')) {
		t->captionHeight =
			FONTHEIGHT(hw->html.plainbold_font) +
			FIELD_BORDER_SPACE;
		t->height += t->captionHeight;
		}

#ifndef DISABLE_TRACE
	if (htmlwTrace) {
		TableDump(t);
		fprintf(stderr,"TableCalculateDimensions(): table is %d x %d\n",
					t->width,t->height);
	}
#endif

} /* TableCalculateDimensions() */




/* expand colspans and rowspans in table */
/* return True if this routine did something */
static Boolean TableExpandFields(tableList, rowList, rowCount, columnCount)
List tableList;
List rowList;
int rowCount;
int *columnCount;
{
TableField *field;
List previousRow;		/* previous to current row */
TableField *aboveField;		/* field above current field */
TableField *fieldToTheLeft;	/* field to the left of current field */
Boolean expandedSomething;

	expandedSomething = False;
	/* check for and take care of previous rowspans */
	if (rowCount > 1) {
		/* get field above this one */
		previousRow = (List) ListGetIndexedEntry(tableList,
					rowCount - 2);/*zero indexed*/
		aboveField =(TableField *)ListGetIndexedEntry(previousRow,
					*columnCount);
		if (aboveField) {
		    /*check if the above expands into this row*/
		    if (aboveField->rowSpan > 1) {
			if (!(field = NewTableField())) {
				return(0); /* out of memory */
				}

			field->rowSpan = aboveField->rowSpan - 1;
			field->contVert = True;
			field->contHoriz = aboveField->contHoriz;
		        field->header = aboveField->header;

			ListAddEntry(rowList, field);
			expandedSomething = True;
			(*columnCount)++;
			}
		    }
		}

	/* check for and take care of previous colspans */
	if (*columnCount) {
		/* get field above this one */
		fieldToTheLeft = (TableField *) ListTail(rowList);
		while(fieldToTheLeft->colSpan > 1) {

		    if (!(field = NewTableField())) {
			return(0); /* out of memory */
			}

		    field->colSpan = fieldToTheLeft->colSpan - 1;
		    field->contHoriz = True;
		    field->header = fieldToTheLeft->header;

		    if (fieldToTheLeft->rowSpan > 1) {
			field->rowSpan = fieldToTheLeft->rowSpan;
			}

		    ListAddEntry(rowList, field);
		    fieldToTheLeft = field;
		    (*columnCount)++;
		    expandedSomething = True;
		    }
		}

	return(expandedSomething);
}




/* set/get attributes for display from mark list for the field*/
static void TableFieldSetAttributes(hw,field,mptr)
HTMLWidget hw;
TableField *field;
struct mark_up *mptr;
{
struct mark_up *m;
int len;

/* Gather ALL the text runs between this cell's tag and the next
   cell/row/table boundary (the old code kept only the first run, so
   everything after the first inline tag in a cell was lost).  Along
   the way note the cell's first link, its first image, and a font
   for the whole cell.  The parser already expanded entities in the
   text, so it must not be clean_text()ed a second time (that eats
   literal '&'s). */

	char *cur_href = (char *) 0;	/* anchor currently open */
	XFontStruct *fstack[8];		/* nested inline font markup */
	int fdepth = 0;
	XFontStruct *base_font;
	XFontStruct *cur_font;

	/* cells read like the document: the regular proportional font,
	   bold for headers (the old code used the plain/mono font) */
	base_font = field->header ?
		hw->html.bold_font : hw->html.font;
	if (base_font == (XFontStruct *) 0) {
		base_font = hw->html.plain_font;
		}
	field->font = base_font;	/* fallback for run-less content */
	cur_font = base_font;
	len = 0;
	m = mptr->next;
	while(m &&
		(!((m->type == M_TABLE)&&(m->is_end))) &&
		(m->type != M_TABLE_ROW) &&
		(m->type != M_TABLE_DATA) && (m->type != M_TABLE_HEADER)) {
		if ((m->type == M_TABLE)&&(!m->is_end)) {
			/*
			 * A nested table: build it here so it interleaves
			 * with the cell's text as an inline item -- a
			 * comment thread is many sibling tables in one
			 * cell.  MakeTable consumes the marks through the
			 * matching end tag; the loop's advance then steps
			 * past it.
			 */
			TableInfo *nested;
			struct mark_up *before = m;

			nested = MakeTable(hw, &m, 0, 0);
			if (nested != (TableInfo *) 0) {
				TableAddRunItem(field, cur_href,
					(ImageInfo *) 0, (WidgetInfo *) 0,
					nested);
				}
			if ((m == (struct mark_up *) 0)||(m == before)) {
				break;
				}
			}
		else if (m->type == M_NONE) {
			char *tp;

			/* skip all-whitespace runs between tags */
			tp = m->text;
			while ((tp != (char *) 0)&&(*tp)&&
				isspace((unsigned char)*tp)) {
				tp++;
				}
			if ((tp != (char *) 0)&&(*tp)) {
				int rl = strlen(m->text);
				CellRun *lr;

				if (field->text == (char *) 0) {
					field->text = (char *)malloc(rl + 1);
					strcpy(field->text, m->text);
					len = rl;
					}
				else {
					field->text = (char *)realloc(
						field->text, len + rl + 2);
					field->text[len] = ' ';
					strcpy(field->text + len + 1,
						m->text);
					len += rl + 1;
					}

				/* record the run with the anchor and font
				   it sits in; merge with the last run when
				   both match */
				lr = (field->run_cnt > 0) ?
					&field->runs[field->run_cnt-1] :
					(CellRun *) 0;
				if ((lr != (CellRun *) 0)&&
				    (lr->font == cur_font)&&
				    (((lr->href == (char *) 0)&&
				      (cur_href == (char *) 0))||
				     ((lr->href != (char *) 0)&&
				      (cur_href != (char *) 0)&&
				      (strcmp(lr->href, cur_href) == 0)))) {
					lr->text = (char *)realloc(lr->text,
						strlen(lr->text) + rl + 2);
					strcat(lr->text, " ");
					strcat(lr->text, m->text);
					}
				else {
					field->runs = (CellRun *)realloc(
						field->runs,
						(field->run_cnt + 1) *
						sizeof(CellRun));
					lr = &field->runs[field->run_cnt];
					lr->text = strdup(m->text);
					lr->href = (cur_href != (char *) 0) ?
						strdup(cur_href) : (char *) 0;
					lr->font = cur_font;
					lr->image = (ImageInfo *) 0;
					lr->winfo = (WidgetInfo *) 0;
					lr->table = (struct table_rec *) 0;
					lr->linebreak = 0;
					field->run_cnt++;
					}
				}
			}
		else if (((m->type == M_SELECT)||(m->type == M_TEXTAREA)||
			  (m->type == M_BUTTON))&&(!m->is_end)) {
			WidgetInfo *wp;

			/* composite form elements: the bridge scans to
			   the matching end tag and builds the widget, so
			   their inner marks never become cell text */
			if (m->type == M_SELECT) {
				wp = TableMakeSelectWidget(hw, &m);
				}
			else if (m->type == M_TEXTAREA) {
				wp = TableMakeTextAreaWidget(hw, &m);
				}
			else {
				wp = TableMakeButtonWidget(hw, &m);
				}
			if ((wp != (WidgetInfo *) 0)&&(wp->w != NULL)) {
				TableAddRunItem(field, (char *) 0,
					(ImageInfo *) 0, wp,
					(TableInfo *) 0);
				}
			if ((m == (struct mark_up *) 0)||
			    (m->type == M_TABLE)||
			    (m->type == M_TABLE_ROW)||
			    (m->type == M_TABLE_DATA)||
			    (m->type == M_TABLE_HEADER)) {
				/* stopped at a boundary: cell is over */
				break;
				}
			}
		else if (m->is_end) {
		    switch(m->type) {
			case M_ANCHOR:
					if (cur_href != (char *) 0) {
						free(cur_href);
						cur_href = (char *) 0;
						}
					break;
			case M_ITALIC:
			case M_VARIABLE:
			case M_EMPHASIZED:
			case M_BOLD:
			case M_STRONG:
			case M_FIXED:
			case M_CODE:
			case M_SAMPLE:
			case M_KEYBOARD:
					/* pop the inline font */
					if (fdepth > 0) {
						cur_font = fstack[--fdepth];
						}
					else {
						cur_font = base_font;
						}
					break;
			}
		    }
		else {
		    switch(m->type) {
			case M_ITALIC:
			case M_VARIABLE:
			case M_EMPHASIZED:
					if (fdepth < 8) {
						fstack[fdepth++] = cur_font;
						}
					cur_font = hw->html.italic_font;
					break;
			case M_BOLD:
			case M_STRONG:
					if (fdepth < 8) {
						fstack[fdepth++] = cur_font;
						}
					cur_font = hw->html.bold_font;
					break;
			case M_ANCHOR:
					/* links get their color and
					   underline per run; no font
					   change, like the main flow */
					if (cur_href != (char *) 0) {
						free(cur_href);
						}
					cur_href = (m->start != (char *) 0) ?
						ParseMarkTag(m->start,
							MT_ANCHOR, "HREF") :
						(char *) 0;
					/* the first link also serves whole-
					   cell content (images, widgets) */
					if ((field->href == (char *) 0)&&
					    (cur_href != (char *) 0)) {
						field->href =
							strdup(cur_href);
						}
					break;
			case M_IMAGE:
					/* an inline image item, flowed with
					   the text (and hot when inside an
					   anchor) */
					if ((m->start != (char *) 0)&&
					    (hw->html.resolveImage != NULL)) {
						char *isrc;
						ImageInfo *img;

						isrc = ParseMarkTag(m->start,
							MT_IMAGE, "SRC");
						if (isrc != (char *) 0) {
						    img = (ImageInfo *)
							(*(resolveImageProc)
							(hw->html.resolveImage))
							((Widget)hw, isrc,
							 0, NULL, NULL);
						    free(isrc);
						    if ((img != (ImageInfo *) 0)&&
							(img->width > 0)) {
							TableAddRunItem(field,
							    cur_href, img,
							    (WidgetInfo *) 0,
							    (TableInfo *) 0);
							}
						    }
						}
					break;
			case M_INPUT:
					/* an inline form widget item.
					   Hidden inputs register with the
					   form but have no widget to show. */
					if (m->start != (char *) 0) {
						WidgetInfo *wp;

						wp = TableMakeWidget(hw,
							m->start);
						if ((wp != (WidgetInfo *) 0)&&
						    (wp->w != NULL)) {
							TableAddRunItem(field,
							    (char *) 0,
							    (ImageInfo *) 0,
							    wp,
							    (TableInfo *) 0);
							}
						}
					break;
			case M_FIXED:
			case M_CODE:
			case M_SAMPLE:
			case M_KEYBOARD:
					if (fdepth < 8) {
						fstack[fdepth++] = cur_font;
						}
					cur_font = hw->html.fixed_font;
					break;
			case M_LINEBREAK:
					TableAddBreak(field, 1);
					break;
			case M_PARAGRAPH:
					/* a blank line between paragraphs;
					   nothing at the top of the cell */
					if (field->run_cnt > 0) {
						TableAddBreak(field, 2);
						}
					break;
			}
		    }
		m = m->next;
		}

	if (field->run_cnt > 0) {
		char *p;
		int ri;

		/* flatten embedded newlines/tabs in the text runs */
		if (field->text != (char *) 0) {
			for (p = field->text; *p; p++) {
				if (isspace((unsigned char)*p)) {
					*p = ' ';
					}
				}
			}
		for (ri = 0; ri < field->run_cnt; ri++) {
			if (field->runs[ri].text == (char *) 0) {
				continue;
				}
			for (p = field->runs[ri].text; *p; p++) {
				if (isspace((unsigned char)*p)) {
					*p = ' ';
					}
				}
			}
		field->type = F_TEXT;
		}

	if (cur_href != (char *) 0) {
		free(cur_href);
		}
}


TableInfo *MakeTable(hw, mptr, x, y)
HTMLWidget hw;
struct mark_up **mptr;
unsigned int x,y;
{
struct mark_up *m;
TableInfo *t;
TableField *field;
int columnCount;
int rowCount;
char *val;
List rowList; 			/* current row (List of TableFields)*/
List tableList;			/* list of Row Lists */
char *tptr;
Pixel rowBg;			/* BGCOLOR from the current <tr> */
int rowHasBg;
int rowValign;			/* VALIGN from the current <tr> */

	if (((*mptr)->type != M_TABLE) || ((*mptr)->is_end)) {
		return(0);
		}
	if (!(t = (TableInfo *) malloc(sizeof(TableInfo)))) {
		return(0);
		}
	t->numColumns = 0;
	t->numRows = 0;
	t->caption = (char *) 0;
	t->captionAlignment = ALIGN_TOP;
	t->captionHeight = 0;

	if (tptr=ParseMarkTag(((*mptr)->start),MT_TABLE,"BORDER")) {
		t->borders = atoi(tptr);
		}
	else {
		t->borders = 0;
		}
	tptr = ParseMarkTag(((*mptr)->start),MT_TABLE,"WIDTH");
	TableParseWidth(tptr, &t->reqWidth, &t->reqPercent);
	t->has_bg = False;
	t->bg = (Pixel) 0;
	tptr = ParseMarkTag(((*mptr)->start),MT_TABLE,"BGCOLOR");
	if (tptr != (char *) 0) {
		if (HTMLAllocColor((Widget)hw, tptr, &t->bg)) {
			t->has_bg = True;
			}
		}
	/* defaults close to the classic hardcoded look (an inset of 5
	   per cell): pad 2 + space 2 */
	t->cellspacing = 2;
	t->cellpadding = 2;
	if (tptr = ParseMarkTag(((*mptr)->start),MT_TABLE,"CELLSPACING")) {
		t->cellspacing = atoi(tptr);
		if (t->cellspacing < 0) {
			t->cellspacing = 0;
			}
		if (t->cellspacing > 50) {
			t->cellspacing = 50;
			}
		}
	if (tptr = ParseMarkTag(((*mptr)->start),MT_TABLE,"CELLPADDING")) {
		t->cellpadding = atoi(tptr);
		if (t->cellpadding < 0) {
			t->cellpadding = 0;
			}
		if (t->cellpadding > 50) {
			t->cellpadding = 50;
			}
		}
	tableList = ListCreate();
	rowList = ListCreate();
	ListAddEntry(tableList, rowList);
	columnCount = 0;
	rowCount=1;
	rowBg = (Pixel) 0;
	rowHasBg = 0;
	rowValign = ALIGN_MIDDLE;
	m = *mptr;
	field = (TableField *) 0;
	while (m && (!((m->type == M_TABLE) && (m->is_end)))) {

		if ((m->type == M_TABLE) && (!m->is_end) && (m != *mptr)) {
			/*
			 * A nested table: the cell scanner has already
			 * built it as an inline item of its cell.  Just
			 * skip its marks so the inner rows never leak
			 * into this grid.
			 */
			int depth = 1;

			m = m->next;
			while ((m != (struct mark_up *) 0)&&(depth > 0)) {
				if (m->type == M_TABLE) {
					depth += (m->is_end) ? -1 : 1;
					}
				if (depth > 0) {
					m = m->next;
					}
				}
			if (m == (struct mark_up *) 0) {
				break;
				}
			}

		else if (m->type == M_CAPTION) {
			if (!m->is_end) {
				char *aval;

				/* captions draw on top unless asked not to
				   (modern default; the old code showed them
				   on the bottom and dropped the text) */
				aval = ParseMarkTag(m->start,MT_CAPTION,
					"ALIGN");
				if (caseless_equal(aval,"bottom")) {
					t->captionAlignment = ALIGN_BOTTOM;
					}
				else {
					t->captionAlignment = ALIGN_TOP;
					}
				if (aval != (char *) 0) {
					free(aval);
					}

				/* collect the caption's text runs; stop at
				   the first non-text mark so a missing end
				   tag cannot eat the table */
				while ((m->next != (struct mark_up *) 0)&&
					(m->next->type == M_NONE)) {
					m = m->next;
					if (m->text == (char *) 0) {
						continue;
						}
					if (t->caption == (char *) 0) {
						t->caption = strdup(m->text);
						}
					else {
						t->caption = (char *)realloc(
						    t->caption,
						    strlen(t->caption) +
						    strlen(m->text) + 2);
						strcat(t->caption, " ");
						strcat(t->caption, m->text);
						}
					}
				if (t->caption != (char *) 0) {
					char *cp;

					for (cp = t->caption; *cp; cp++) {
						if (isspace((unsigned char)*cp)) {
							*cp = ' ';
							}
						}
					}
				}
			}

		else if ((m->type == M_TABLE_ROW)&&(!m->is_end)) {
			/* the row's BGCOLOR is the default for its cells */
			rowHasBg = 0;
			val = ParseMarkTag(m->start,MT_TABLE_ROW,"bgcolor");
			if (val != (char *) 0) {
				if (HTMLAllocColor((Widget)hw, val, &rowBg)) {
					rowHasBg = 1;
					}
				}
			/* likewise the row's VALIGN */
			val = ParseMarkTag(m->start,MT_TABLE_ROW,"valign");
			rowValign = TableParseValign(val, ALIGN_MIDDLE);

			/* expand at end of row */
			while(TableExpandFields(tableList, rowList,
						rowCount, &columnCount));

			/* if: is this the first container <tr> or the
			       separator */
			if (ListHead(ListHead(tableList))) {
				rowList = ListCreate();
				ListAddEntry(tableList,rowList);
				rowCount++;
				}
			columnCount = 0;
			/* expand cols at beginning of row */
			TableExpandFields(tableList, rowList,
						rowCount, &columnCount);
			}

		else if ((m->type == M_TABLE_DATA) && (!m->is_end))  {

			while(TableExpandFields(tableList, rowList,
						rowCount, &columnCount));

			if (!(field = NewTableField())) {
				return(0); /* out of memory */
				}
			field->header = False;

			/* check for colspan & rowspan */

			val = ParseMarkTag(m->start,MT_TABLE_DATA,"colspan");
			if (val) {
			    field->colSpan = atoi(val);
			    if ((field->colSpan > 100)||(field->colSpan < 1)){
				field->colSpan = 1;
				}
			    }

			val = ParseMarkTag(m->start,MT_TABLE_DATA,"rowspan");
			if (val) {
			    field->rowSpan = atoi(val);
			    if ((field->rowSpan > 100)||(field->rowSpan < 1)){
				field->rowSpan = 1;
				}
			    }

			/* check for alignment */
			val = ParseMarkTag(m->start,MT_TABLE_DATA,"align");
			if (caseless_equal(val,"center")) {
				field->alignment = ALIGN_CENTER;
				}
			else if (caseless_equal(val,"right")) {
				field->alignment = ALIGN_RIGHT;
				}
			else {
				/* like HTML says: td aligns left unless
				   asked otherwise (th centers) */
				field->alignment = ALIGN_LEFT;
				}

			val = ParseMarkTag(m->start,MT_TABLE_DATA,"width");
			TableParseWidth(val, &field->reqWidth,
				&field->reqPercent);

			val = ParseMarkTag(m->start,MT_TABLE_DATA,"bgcolor");
			if ((val != (char *) 0)&&
			    (HTMLAllocColor((Widget)hw, val, &field->bg))) {
				field->has_bg = True;
				}
			else if (rowHasBg) {
				field->bg = rowBg;
				field->has_bg = True;
				}

			val = ParseMarkTag(m->start,MT_TABLE_DATA,"valign");
			field->valign = TableParseValign(val, rowValign);

			TableFieldSetAttributes(hw,field,m);

			ListAddEntry(rowList, field);
			columnCount++;
			}

		else if ((m->type == M_TABLE_HEADER) && (!m->is_end)) {

			while(TableExpandFields(tableList, rowList,
						rowCount, &columnCount));

			if (!(field = NewTableField())) {
				return(0); /*out of memory */
				}
			field->header = True;

			val = ParseMarkTag(m->start,MT_TABLE_HEADER,"colspan");
			if (val) {
				field->colSpan = atoi(val);
				if ((field->colSpan > 100)||(field->colSpan<1)){
					field->colSpan = 1;
					}
				}

			val = ParseMarkTag(m->start,MT_TABLE_HEADER,"rowspan");
			if (val) {
				field->rowSpan = atoi(val);
				if ((field->rowSpan > 100) || (field->rowSpan < 1)) {
					field->rowSpan = 1;
					}
				}

			/* check for alignment */
			val = ParseMarkTag(m->start,MT_TABLE_HEADER,"align");
			if (caseless_equal(val,"left")) {
				field->alignment = ALIGN_LEFT;
				}
			else if (caseless_equal(val,"right")) {
				field->alignment = ALIGN_RIGHT;
				}
			else {
				field->alignment = ALIGN_CENTER;
				}

			val = ParseMarkTag(m->start,MT_TABLE_HEADER,"width");
			TableParseWidth(val, &field->reqWidth,
				&field->reqPercent);

			val = ParseMarkTag(m->start,MT_TABLE_HEADER,"bgcolor");
			if ((val != (char *) 0)&&
			    (HTMLAllocColor((Widget)hw, val, &field->bg))) {
				field->has_bg = True;
				}
			else if (rowHasBg) {
				field->bg = rowBg;
				field->has_bg = True;
				}

			val = ParseMarkTag(m->start,MT_TABLE_HEADER,"valign");
			field->valign = TableParseValign(val, rowValign);

			TableFieldSetAttributes(hw,field,m);

			ListAddEntry(rowList, field);
			columnCount++;
			}
		else if (m->type == 0){ /* text */
			/* taken care of by TableFieldSetAttributes() above*/
			/* so ignore text now */
			}


		m = m->next;
		}
	*mptr = m; /* advance mark pointer to end of table */

	/* end of table has been hit, so wrap it up */
	/* clean up any at end of row */
	while(TableExpandFields(tableList, rowList, rowCount, &columnCount));
/*
	rowCount++;
	do {
		rowList = ListCreate();
		ListAddEntry(tableList,rowList);
		rowCount++;
		}
	while (TableExpandFields(tableList, ListTail(tableList),
			ListCount(tableList) - 1, 0));
	ListDeleteEntry(tableList, rowList);
	rowCount--;
*/

	if (!(TableCleanUp(t,tableList))) {
		return(0); /* out of memory */
		}

	/* free up memory from tableList since TableCleanUp
	   has already copied it into an array for speed. */
	rowList = (List) ListHead(tableList);
	while (rowList) {
		field = (TableField *) ListHead(rowList);
		while(field) {
			ListDeleteEntry(rowList,field);
			free(field);
			field = (TableField *) ListHead(rowList);
			}
		ListDeleteEntry(tableList,rowList);
		ListDestroy(rowList);
		rowList = (List) ListHead(tableList);
		}

	{
		int pageWidth;

		/* lay the table out for the real view, not the 622
		   pixels the original code hardcoded */
		pageWidth = (int)hw->html.view_width -
			(int)(2 * hw->html.margin_width);
		if (pageWidth < 300) {
			pageWidth = 622;
			}
		TableCalculateDimensions(hw,t,pageWidth);
	}

#ifndef DISABLE_TRACE
	if (htmlwTrace) {
		TableDump(t);
	}
#endif

	return(t);

} /* MakeTable() */




TableDisplayField(hw,eptr,field,x,y,width,height,pad)
HTMLWidget hw;
struct ele_rec *eptr;
TableField *field;
int x,y; /* field origin */
int width,height; /* space allowed for displaying */
int pad; /* the table's cellpadding */
{
int stringWidth; /* in pixels */
int placeX,placeY;
int lineHeight;
int baseLine;
int yy;

	if (field->type == F_NONE) { /* nothing to display in field */
		return -1;
		}

	if (field->type != F_TEXT) { /* everything else is flow content */
		return -1;
		}

	/* adjust for the cell padding */
	width -= (2 * pad);
	x += pad;
	height -= (2 * pad);
	y += pad;

	XSetLineAttributes(XtDisplay(hw),hw->html.drawGC,1,LineSolid,
		CapNotLast,JoinMiter);

	/* flow the runs into the cell: text with per-run anchor color,
	   font and underline; images and widgets as inline items */
	{
		int th;

		TableCellFlow(hw, eptr, field, x, y, width, height,
			CELLFLOW_DRAW, 0, 0, &th,
			(int *) 0, (int *) 0);
	}

	XSetForeground(XtDisplay(hw), hw->html.drawGC, eptr->fg);
	XSetLineAttributes(XtDisplay(hw),
			   hw->html.drawGC,
			   eptr->table_data->borders,
			   LineSolid,
			   CapNotLast,
			   JoinMiter);

} /* TableDisplayField() */



/* Find actual table field dimensions considering colspans & rowspans */
static void TableGetExpandedDimensions(t,xpos,ypos,
						expandWidth,expandHeight)
TableInfo *t;
int xpos,ypos; /* current field index */
int *expandWidth,*expandHeight; /* returned */
{
int x,y;
	x = xpos;
	y = ypos;

	*expandWidth = t->table[y * t->numColumns + x].colWidth;
	*expandHeight = t->table[y * t->numColumns + x].rowHeight;

	/* do width; a span swallows the spacing between its cells */
	x++;
	if (x < t->numColumns) {
		/* do width */
		while ((x < t->numColumns) &&
				t->table[y * t->numColumns + x].contHoriz) {
			(*expandWidth) += t->table[y * t->numColumns + x].colWidth
				+ t->cellspacing;
			x++;
			}
		}

	x = xpos;
	y++;
	if (y < t->numRows) {
		/* do height */
		while ((y < t->numRows) &&
				t->table[y * t->numColumns + x].contVert) {
			(*expandHeight) += t->table[y * t->numColumns+x].rowHeight
				+ t->cellspacing;
			y++;
			}
		}


}




/* draw a table (and, through TableDisplayField, any tables nested
   in its cells) with its origin at x,y in view coordinates */
static void TableDraw(hw,eptr,t,x,y)
HTMLWidget hw;
struct ele_rec *eptr;
TableInfo *t;
int x,y; 		/* table origin, already scroll adjusted */
{
register int xx,yy;
TableField *field;
int vertMarker,horizMarker;
int colWidth,rowHeight;
int expandedWidth,expandedHeight;

	if (t == NULL) {
		return;
		}

	XSetLineAttributes(XtDisplay(hw),
			   hw->html.drawGC,
			   t->borders,
			   LineSolid,
			   CapNotLast,
			   JoinMiter);
	XSetForeground(XtDisplay(hw), hw->html.drawGC, eptr->fg);
	XSetBackground(XtDisplay(hw), hw->html.drawGC, eptr->bg);

	if ((t->caption != NULL)&&(t->captionHeight > 0)) {
		XFontStruct *cfont = hw->html.plainbold_font;
		int capx, capy, capw;
		XmString cs;
		XmFontList cfl;

		capw = HTMLTextWidth(cfont, t->caption, strlen(t->caption));
		capx = x + (t->width - capw) / 2;
		if (capx < x) {
			capx = x;
			}
		if (t->captionAlignment == ALIGN_BOTTOM) {
			capy = y + t->height - t->captionHeight;
			}
		else {
			capy = y;
			/* the grid starts below a top caption */
			y += t->captionHeight;
			}
		XSetFont(XtDisplay(hw), hw->html.drawGC, cfont->fid);
		cs = XmStringCreateLocalized(t->caption);
		cfl = XmFontListCreate(cfont, XmSTRING_DEFAULT_CHARSET);
		XmStringDraw(XtDisplay(hw), XtWindow(hw->html.view),
			cfl, cs, hw->html.drawGC,
			capx, capy,
			XmStringWidth(cfl, cs),
			XmALIGNMENT_BEGINNING,
			XmSTRING_DIRECTION_L_TO_R, NULL);
		XmStringFree(cs);
		XmFontListFree(cfl);
		}
	else if ((t->captionHeight > 0)&&
		(t->captionAlignment != ALIGN_BOTTOM)) {
		y += t->captionHeight;
		}

	/* the table's own background, under everything (it also shows
	   through the cell-spacing gaps between colored cells) */
	if (t->has_bg) {
		XSetForeground(XtDisplay(hw), hw->html.drawGC, t->bg);
		XFillRectangle(XtDisplay(hw), XtWindow(hw->html.view),
			hw->html.drawGC, x, y,
			(unsigned int)t->width,
			(unsigned int)(t->height - t->captionHeight));
		XSetForeground(XtDisplay(hw), hw->html.drawGC, eptr->fg);
		}

	field = t->table;
	horizMarker = y + t->borders + t->cellspacing;
	for (yy = 0; yy < t->numRows; yy++) {
		vertMarker = x + t->borders + t->cellspacing;
		rowHeight = field->rowHeight;
		for (xx = 0; xx < t->numColumns; xx++) {
			colWidth = field->colWidth;

			/* the cell's background first, so grid lines and
			   contents draw over it; continuation fields are
			   covered by their anchor's expanded fill */
			if ((field->has_bg)&&
			    (!field->contVert)&&(!field->contHoriz)) {
				TableGetExpandedDimensions(t, xx, yy,
					&expandedWidth, &expandedHeight);
				XSetForeground(XtDisplay(hw),
					hw->html.drawGC, field->bg);
				XFillRectangle(XtDisplay(hw),
					XtWindow(hw->html.view),
					hw->html.drawGC,
					vertMarker, horizMarker,
					(unsigned int)expandedWidth,
					(unsigned int)expandedHeight);
				XSetForeground(XtDisplay(hw),
					hw->html.drawGC, eptr->fg);
				}

			/* draw field borders: with spacing the cells are
			   separated, so each anchor cell gets its own
			   rectangle (at zero spacing it coincides with
			   the classic shared grid lines) */
			if ((t->borders)&&
			    (!field->contVert)&&(!field->contHoriz)) {
				TableGetExpandedDimensions(t, xx, yy,
					&expandedWidth, &expandedHeight);
				XDrawRectangle(XtDisplay(hw),
					XtWindow(hw->html.view),
					hw->html.drawGC,
					vertMarker, horizMarker,
					(unsigned int)expandedWidth,
					(unsigned int)expandedHeight);
				}
			TableGetExpandedDimensions(t,
					xx,yy,&expandedWidth,&expandedHeight);
			/* fill in field */
			TableDisplayField(hw,
					  eptr,
					  field,
					  vertMarker,
					  horizMarker,
					  expandedWidth,
					  expandedHeight,
					  t->cellpadding);

			/* a nested TableDraw (a table item in a cell's
			   flow) may have changed the line width; restore
			   ours for the remaining borders */
			if (field->run_cnt > 0) {
				XSetLineAttributes(XtDisplay(hw),
						   hw->html.drawGC,
						   t->borders,
						   LineSolid,
						   CapNotLast,
						   JoinMiter);
				XSetForeground(XtDisplay(hw),
						hw->html.drawGC, eptr->fg);
				XSetBackground(XtDisplay(hw),
						hw->html.drawGC, eptr->bg);
				}

			vertMarker += colWidth + t->cellspacing;
			field++;
			}

		horizMarker += rowHeight + t->cellspacing;
		}

	/* the table's outer frame */
	if (t->borders) {
		XDrawRectangle(XtDisplay(hw), XtWindow(hw->html.view),
			hw->html.drawGC, x, y,
			(unsigned int)t->width,
			(unsigned int)(t->height - t->captionHeight));
		}

	XSetLineAttributes(XtDisplay(hw),
			   hw->html.drawGC,
			   1,
			   LineSolid,
			   CapNotLast,
			   JoinMiter);
}


/* display table */
void TableRefresh(hw,eptr)
HTMLWidget hw;
struct ele_rec *eptr;
{
	if (eptr->table_data == NULL) {
		return;
		}

	TableDraw(hw, eptr, eptr->table_data,
		eptr->x - hw->html.scroll_x,
		eptr->y - hw->html.scroll_y);
}


/* find the anchor (if any) of the table cell under view coordinates
   ex,ey; x,y is the table's origin in the same coordinate space.
   Walks the grid with the same arithmetic TableDraw uses, and
   recurses into nested tables. */
static char *TableAnchorAt(t,x,y,ex,ey)
TableInfo *t;
int x,y;
int ex,ey;
{
register int xx,yy;
TableField *field;
int vertMarker,horizMarker;
int colWidth,rowHeight;
int expandedWidth,expandedHeight;

	if (t == NULL) {
		return((char *) 0);
		}

	/* the grid sits below a top caption */
	if ((t->captionHeight > 0)&&
		(t->captionAlignment != ALIGN_BOTTOM)) {
		y += t->captionHeight;
		}

	field = t->table;
	horizMarker = y + t->borders + t->cellspacing;
	for (yy = 0; yy < t->numRows; yy++) {
		vertMarker = x + t->borders + t->cellspacing;
		rowHeight = field->rowHeight;
		for (xx = 0; xx < t->numColumns; xx++) {
			colWidth = field->colWidth;
			TableGetExpandedDimensions(t,xx,yy,
				&expandedWidth,&expandedHeight);
			if ((ex >= vertMarker)&&
			    (ex < (vertMarker + expandedWidth))&&
			    (ey >= horizMarker)&&
			    (ey < (horizMarker + expandedHeight))) {
				if ((field->type == F_TEXT)&&
				    (field->run_cnt > 0)) {
					int th;

					/* per-run: only the link's own
					   words are hot, with the same
					   flow the draw used */
					return(TableCellFlow(
						(HTMLWidget) 0,
						(struct ele_rec *) 0,
						field,
						vertMarker +
							t->cellpadding,
						horizMarker +
							t->cellpadding,
						expandedWidth -
							2*t->cellpadding,
						expandedHeight -
							2*t->cellpadding,
						CELLFLOW_HIT, ex, ey, &th,
						(int *) 0, (int *) 0));
					}
				return(field->href);
				}
			vertMarker += colWidth + t->cellspacing;
			field++;
			}
		horizMarker += rowHeight + t->cellspacing;
		}
	return((char *) 0);
}


/* Called from the mouse paths in HTML.c after LocateElement: when
   the element under the pointer is a table, stamp the anchor of the
   cell under the pointer onto the element so the ordinary anchor
   machinery (activation, tracking, cursor) works unchanged.  The
   element's anchorHRef is owned by the element (FreeLineList frees
   it), hence the strdup. */
void TableResolveAnchor(hw,eptr,ex,ey)
HTMLWidget hw;
struct ele_rec *eptr;
int ex,ey;
{
char *href;

	if ((eptr == NULL)||(eptr->type != E_TABLE)||
	    (eptr->table_data == NULL)) {
		return;
		}

	href = TableAnchorAt(eptr->table_data,
		eptr->x - hw->html.scroll_x,
		eptr->y - hw->html.scroll_y,
		ex, ey);

	if ((eptr->anchorHRef != NULL)&&(href != NULL)&&
	    (strcmp(eptr->anchorHRef, href) == 0)) {
		return; /* already stamped with this link */
		}
	if (eptr->anchorHRef != NULL) {
		free(eptr->anchorHRef);
		}
	eptr->anchorHRef = (href != NULL) ? strdup(href) : NULL;
}
