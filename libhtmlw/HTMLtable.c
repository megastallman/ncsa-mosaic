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
extern Pixmap InfoToImage();

static TableField *NewTableField()
{
TableField *tf;

	if (!(tf = (TableField *) malloc(sizeof(TableField)))) {
		return(0);
		}
	tf->alignment = ALIGN_CENTER;
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
	tf->font = (XFontStruct *) 0;
	tf->formattedText = (char **) 0;
	tf->numLines = 0;

	tf->image = (ImageInfo *) 0;
	tf->winfo = (WidgetInfo *) 0;
	tf->table = (struct table_rec *) 0;

	return(tf);
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
        stringWidth = XTextWidth(font,text,strlen(text));
	if (stringWidth < width) {
		ListAddEntry(textList,strdup(text));
		}
	else {

	    builtWidth = 0;
	    textPtr = text;
	    spaceWidth = XTextWidth(font," ",1);
	    *tmpBuff = '\0';
	    while (*textPtr) {

#ifndef DISABLE_TRACE
		if (htmlwTrace) {
			printf("textPtr = \"%s\"\n",textPtr);
		}
#endif

		GetWord(textPtr,&wordStart,&wordEnd);
		wordLength = (int) (wordEnd - wordStart);
		wordWidth = XTextWidth(font,wordStart, wordLength);
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
				wordWidth = XTextWidth(font,wordStart,
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
			char *wp, *ws, *we;
			int ww;

			field->maxWidth = XTextWidth(field->font,
					field->text,strlen(field->text));
			field->minHeight = FONTHEIGHT(field->font);
			/* the narrowest this cell can wrap to is its
			   widest single word */
			wp = field->text;
			while (*wp) {
				GetWord(wp,&ws,&we);
				if (we > ws) {
					ww = XTextWidth(field->font, ws,
							(int)(we - ws));
					if (ww > field->minWidth) {
						field->minWidth = ww;
						}
					}
				wp = we;
				}
			}
		else if (field->type == F_TABLE) {
			/* a nested table was laid out by the recursive
			   MakeTable call; it has fixed dimensions */
			field->maxWidth = field->table->width;
			field->minWidth = field->table->width;
			field->minHeight = field->table->height;
			}
		else if (field->type == F_IMAGE) {
			/* an image has fixed dimensions too */
			field->maxWidth = field->image->width;
			field->minWidth = field->image->width;
			field->minHeight = field->image->height;
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


	/* add border spacing to widths */
	sumMaxWidth += (t->numColumns * 2 * FIELD_BORDER_SPACE);
	sumMinWidth += (t->numColumns * 2 * FIELD_BORDER_SPACE);


	/* divy up max width with adjacent continue Horizontal fields*/
	for (y = 0; y < t->numRows; y++) {
		    for (x = 0; x < t->numColumns; x++) {
			numAdjacent = TableHowManyConnectedHorizFields(t,x,y);
			if (numAdjacent) {
			    int xx;
			    for (xx = x; xx < x + numAdjacent; xx++) {
				t->table[y * t->numColumns+xx].maxWidth
					= t->table[y*t->numColumns+x].maxWidth
						/ (numAdjacent+ 1);
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
			    int yy;
			    for (yy = y; yy < y + numAdjacent; yy++) {
				t->table[yy * t->numColumns+x].minHeight
					= t->table[y*t->numColumns+x].minHeight
						/ (numAdjacent + 1);
				}
			    }
			y += numAdjacent;
			}
		    }



	/* fit table to page */
	if (sumMaxWidth < pageWidth ) {
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
						+ 2 * FIELD_BORDER_SPACE;
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
						+ 2 * FIELD_BORDER_SPACE;
				}
			}

		/* take care of formattedText */
		for (y=0; y < t->numRows; y++) {
			for (x=0; x < t->numColumns; x++) {
			    field = &(t->table[y * t->numColumns + x]);
			    field->formattedText =
				    (char **) malloc(sizeof(char *));
			    if (field->text) {
				field->formattedText[0]= strdup(field->text);
				field->numLines = 1;
				}
			    else {
				field->formattedText[0]= (char *) 0;
				field->numLines = 0;
				}
			    }
			}
		}
	else {
	/* will have to squeeze fields downward to fit on page */

		percentToShrink = ((float)pageWidth)/((float)sumMaxWidth);
		for (x = 0; x < t->numColumns; x++) {
			/*find max width of this column */
/*
			maxWidthOfColumn = 0;
			for (y = 0; y < t->numRows; y++) {
				maxWidthOfColumn = (maxWidthOfColumn >
					t->table[y*t->numColumns+x].maxWidth) ?
					maxWidthOfColumn :
					t->table[y*t->numColumns+x].maxWidth;
				}
*/

			/* format it */
			for (y = 0; y < t->numRows; y++) {
				field = &(t->table[y*t->numColumns+x]);
				field->colWidth = (int) (percentToShrink *
				     ((float) CalculateMaxWidthOfColumn(t,x)));
				/* never squeeze below the longest word
				   (or a nested table); overflowing the
				   page beats unreadable sliver columns */
				if (field->colWidth < field->minWidth) {
					field->colWidth = field->minWidth;
					}
				field->rowHeight = 0;
				numAdjacent = TableHowManyConnectedHorizFields
									(t,x,y);
				/* calculate the width including connected */
				accumulateColWidth = field->colWidth;
				for (xx = x+1; xx < x+numAdjacent+1; xx++) {
				    accumulateColWidth += (
					(percentToShrink *
                                        ((float) CalculateMaxWidthOfColumn(t,xx))));
				    }

#ifndef DISABLE_TRACE
				if (htmlwTrace) {
					fprintf(stderr,"About to call PourText\n");
				}
#endif

				PourText(field->text,field->font,
					accumulateColWidth,
					&(field->rowHeight),
					 hw->html.percent_vert_space,
					&(field->formattedText),
					&(field->numLines));

				/* fixed-size contents (nested tables and
				   images) cannot be squeezed: they keep
				   the dimensions layout gave them */
				if (field->type == F_TABLE) {
					field->rowHeight = field->table->height
						+ 2 * FIELD_BORDER_SPACE;
					if (field->colWidth <
						(field->table->width +
						 2 * FIELD_BORDER_SPACE)) {
						field->colWidth =
							field->table->width
							+ 2 * FIELD_BORDER_SPACE;
						}
					}
				else if (field->type == F_IMAGE) {
					field->rowHeight = field->image->height
						+ 2 * FIELD_BORDER_SPACE;
					if (field->colWidth <
						(field->image->width +
						 2 * FIELD_BORDER_SPACE)) {
						field->colWidth =
							field->image->width
							+ 2 * FIELD_BORDER_SPACE;
						}
					}

#ifndef DISABLE_TRACE
				if (htmlwTrace) {
					fprintf(stderr,"poured field %d,%d is dims %d,%d: %%shrink=%f\n",
						x,y,field->colWidth,field->rowHeight,percentToShrink);
				}
#endif

				}
			}

		/* divy up width with adjacent continue Horizontal fields*/
		for (y = 0; y < t->numRows; y++) {
		    for (x = 0; x < t->numColumns; x++) {
			numAdjacent = TableHowManyConnectedHorizFields(t,x,y);
			if (numAdjacent) {
			    int xx;
			    for (xx = x; xx < x + numAdjacent; xx++) {
				t->table[y * t->numColumns+xx].colWidth
					= t->table[y*t->numColumns+x].colWidth
						/ (numAdjacent+ 1);
				}
			    }
			x += numAdjacent;
			}
		    }
		/* divy up height with adjacent continue Vertical fields */
		for (x = 0; x < t->numColumns; x++) {
		    for (y = 0; y < t->numRows; y++) {
			numAdjacent = TableHowManyConnectedVertFields(t,x,y);
			if (numAdjacent) {
			    int yy;
			    for (yy = y; yy < y + numAdjacent; yy++) {
				t->table[yy * t->numColumns+x].rowHeight
					= t->table[y*t->numColumns+x].rowHeight
						/ (numAdjacent + 1);
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
		    /* fixed-size contents cannot shrink: floor the column
		       at their width, even if that overflows the page */
		    for (y = 0; y < t->numRows; y++) {
			int fw;

			field = &(t->table[y*t->numColumns+x]);
			fw = 0;
			if (field->type == F_TABLE) {
				fw = field->table->width;
				}
			else if (field->type == F_IMAGE) {
				fw = field->image->width;
				}
			if ((fw > 0)&&
			    (maxWidthOfColumn < (fw +
						2 * FIELD_BORDER_SPACE))) {
				maxWidthOfColumn = fw +
						2 * FIELD_BORDER_SPACE;
				}
			}
		    /* make sure they are all the same */
		    for (y = 0; y < t->numRows; y++) {
			t->table[y*t->numColumns+x].colWidth = maxWidthOfColumn;
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
	t->width+=(t->borders*2);
	t->height+=(t->borders*2);

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

	field->font = hw->html.plain_font; /* default font */
	len = 0;
	m = mptr->next;
	while(m && (m->type != M_TABLE) && (m->type != M_TABLE_ROW) &&
		(m->type != M_TABLE_DATA) && (m->type != M_TABLE_HEADER)) {
		if (m->type == M_NONE) {
			char *tp;

			/* skip all-whitespace runs between tags */
			tp = m->text;
			while ((tp != (char *) 0)&&(*tp)&&
				isspace((unsigned char)*tp)) {
				tp++;
				}
			if ((tp != (char *) 0)&&(*tp)) {
				int rl = strlen(m->text);

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
				}
			}
		else if (!m->is_end) {
		    switch(m->type) {
			case M_ITALIC:
			case M_VARIABLE:
			case M_EMPHASIZED:
					field->font = hw->html.italic_font;
					break;
			case M_BOLD:
			case M_STRONG:
					field->font = hw->html.bold_font;
					break;
			case M_ANCHOR:
					field->font = hw->html.bold_font;
					/* first link in the cell wins; the
					   whole cell becomes that link */
					if ((field->href == (char *) 0)&&
					    (m->start != (char *) 0)) {
						field->href = ParseMarkTag(
							m->start,
							MT_ANCHOR, "HREF");
						}
					break;
			case M_IMAGE:
					/* first image in the cell wins */
					if ((field->image ==
						(ImageInfo *) 0)&&
					    (m->start != (char *) 0)&&
					    (hw->html.resolveImage != NULL)) {
						char *isrc;

						isrc = ParseMarkTag(m->start,
							MT_IMAGE, "SRC");
						if (isrc != (char *) 0) {
						    field->image = (ImageInfo *)
							(*(resolveImageProc)
							(hw->html.resolveImage))
							((Widget)hw, isrc,
							 0, NULL, NULL);
						    free(isrc);
						    }
						}
					break;
			case M_FIXED:
			case M_CODE:
			case M_SAMPLE:
			case M_KEYBOARD:
					field->font = hw->html.fixed_font;
					break;
			}
		    }
		m = m->next;
		}

	if (field->header) {
		field->font = hw->html.plainbold_font;
		}
	if (field->text != (char *) 0) {
		char *p;

		/* flatten embedded newlines/tabs: the single-line
		   display branch draws the raw string */
		for (p = field->text; *p; p++) {
			if (isspace((unsigned char)*p)) {
				*p = ' ';
				}
			}
		field->type = F_TEXT;
		}
	else if ((field->image != (ImageInfo *) 0)&&
		 (field->image->width > 0)) {
		field->type = F_IMAGE;
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
	tableList = ListCreate();
	rowList = ListCreate();
	ListAddEntry(tableList, rowList);
	columnCount = 0;
	rowCount=1;
	m = *mptr;
	field = (TableField *) 0;
	while (m && (!((m->type == M_TABLE) && (m->is_end)))) {

		if ((m->type == M_TABLE) && (!m->is_end) && (m != *mptr)) {
			/*
			 * A nested table: build it recursively and attach
			 * it to the current cell.  The recursive call
			 * consumes the marks up to the matching end tag,
			 * so the inner rows never leak into this grid.
			 */
			TableInfo *nested;
			struct mark_up *before;

			before = m;
			nested = MakeTable(hw, &m, x, y);
			if (nested != (TableInfo *) 0) {
				if ((field != (TableField *) 0)&&
					(field->table == (struct table_rec *) 0)) {
					field->type = F_TABLE;
					field->table = (struct table_rec *) nested;
					}
				/* with no enclosing cell (or a second
				   table in one cell) the content is
				   dropped -- same as any other markup
				   this simple cell model cannot hold */
				}
			else if (m == before) {
				/*
				 * The recursion failed without consuming
				 * anything (out of memory); skip the inner
				 * marks so they cannot masquerade as our
				 * own rows.
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
			if (caseless_equal(val,"left")) {
				field->alignment = ALIGN_LEFT;
				}
			else if (caseless_equal(val,"right")) {
				field->alignment = ALIGN_RIGHT;
				}
			else {
				field->alignment = ALIGN_CENTER;
				}
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




TableDisplayField(hw,eptr,field,x,y,width,height)
HTMLWidget hw;
struct ele_rec *eptr;
TableField *field;
int x,y; /* field origin */
int width,height; /* space allowed for displaying */
{
int stringWidth; /* in pixels */
int placeX,placeY;
int lineHeight;
int baseLine;
int yy;

	if (field->type == F_NONE) { /* nothing to display in field */
		return -1;
		}

	if (field->type == F_TABLE) {
		/* recursively draw a nested table inside this cell */
		TableDraw(hw, eptr, (TableInfo *)field->table,
			x + FIELD_BORDER_SPACE,
			y + FIELD_BORDER_SPACE);
		return 0;
		}

	if (field->type == F_IMAGE) {
		ImageInfo *pic = field->image;
		int ix, iy;

		if ((pic->image == None)&&(pic->image_data != NULL)) {
			pic->image = InfoToImage(hw, pic, 0);
			}
		if (pic->image != None) {
			/* center the image in its cell */
			ix = x + (width - pic->width) / 2;
			iy = y + (height - pic->height) / 2;
			if (ix < (x + FIELD_BORDER_SPACE)) {
				ix = x + FIELD_BORDER_SPACE;
				}
			if (iy < (y + FIELD_BORDER_SPACE)) {
				iy = y + FIELD_BORDER_SPACE;
				}
			XCopyArea(XtDisplay(hw), pic->image,
				XtWindow(hw->html.view), hw->html.drawGC,
				0, 0, pic->width, pic->height, ix, iy);
			}
		return 0;
		}

	if (field->type != F_TEXT) { /* routine only does text at this time */
		return -1;
		}

	/* adjust for aesthetic surounding space */
	width -= (2 * FIELD_BORDER_SPACE);
	x += FIELD_BORDER_SPACE;
	height -= (2 * FIELD_BORDER_SPACE);
	y += FIELD_BORDER_SPACE;


	lineHeight = FONTHEIGHT(field->font);
	baseLine = field->font->max_bounds.ascent;
	placeY = y + (height - (lineHeight * field->numLines))/2;
	for (yy = 0; yy < field->numLines; yy++) {
		stringWidth = XTextWidth(field->font,field->formattedText[yy],
					strlen(field->formattedText[yy]));

		switch(field->alignment) {
			case ALIGN_LEFT:
					placeX = x;
					break;
			case ALIGN_CENTER:
					placeX = x + (width - stringWidth)/2;
					break;
			case ALIGN_RIGHT:
					placeX = x + width - stringWidth;
					break;
			}
/*
		placeY = y + height/2 +
	   			(field->font->max_bounds.ascent
				- field->font->max_bounds.descent)/2;
*/

		XSetLineAttributes(XtDisplay(hw),hw->html.drawGC,1,LineSolid,
			CapNotLast,JoinMiter);
		XSetBackground(XtDisplay(hw), hw->html.drawGC, eptr->bg);
		XSetForeground(XtDisplay(hw), hw->html.drawGC,
			(field->href != (char *) 0) ?
				hw->html.anchor_fg : eptr->fg);
		XSetFont(XtDisplay(hw), hw->html.drawGC, field->font->fid);
		XmString ttd=XmStringCreateLocalized(field->formattedText[yy]);
                XmFontList tftd=XmFontListCreate(field->font,XmSTRING_DEFAULT_CHARSET); 
                XmStringDraw(XtDisplay(hw),
                            XtWindow(hw->html.view),
                            tftd,
                            ttd,
                            hw->html.drawGC,
                            placeX,
                            placeY, /* XmStringDraw wants the TOP,
                                       not the baseline */
                            XmStringWidth(tftd,ttd),
                            XmALIGNMENT_BEGINNING,
                            XmSTRING_DIRECTION_L_TO_R,
                            NULL);
               XmStringFree(ttd);
               XmFontListFree(tftd);

		if (field->href != (char *) 0) {
			/* underline the cell's text like other anchors */
			XDrawLine(XtDisplay(hw), XtWindow(hw->html.view),
				hw->html.drawGC,
				placeX, placeY + baseLine + 1,
				placeX + stringWidth, placeY + baseLine + 1);
			}

		placeY += lineHeight;
		}

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

	/* do width */
	x++;
	if (x < t->numColumns) {
		/* do width */
		while ((x < t->numColumns) &&
				t->table[y * t->numColumns + x].contHoriz) {
			(*expandWidth) += t->table[y * t->numColumns + x].colWidth;
			x++;
			}
		}

	x = xpos;
	y++;
	if (y < t->numRows) {
		/* do height */
		while ((y < t->numRows) &&
				t->table[y * t->numColumns + x].contVert) {
			(*expandHeight) += t->table[y * t->numColumns+x].rowHeight;
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

		capw = XTextWidth(cfont, t->caption, strlen(t->caption));
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

	field = t->table;
	horizMarker = y+t->borders;
	for (yy = 0; yy < t->numRows; yy++) {
		vertMarker = x+(t->borders/2);
		rowHeight = field->rowHeight;
		for (xx = 0; xx < t->numColumns; xx++) {
			colWidth = field->colWidth;

			/* draw field borders */
			if (t->borders){
			    if (!field->contVert) { /* draw above line */
				XDrawLine(XtDisplay(hw),
					XtWindow(hw->html.view),
                        		hw->html.drawGC,
					vertMarker, horizMarker,
					vertMarker + colWidth, horizMarker);
				}
			    if (!field->contHoriz) { /* draw left side*/
				XDrawLine(XtDisplay(hw),
					XtWindow(hw->html.view),
                        		hw->html.drawGC,
					vertMarker, horizMarker,
					vertMarker, horizMarker + rowHeight);
				}
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
					  expandedHeight);

			/* a nested TableDraw may have changed the line
			   width; restore ours for the remaining borders */
			if (field->type == F_TABLE) {
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

			vertMarker += colWidth;
			field++;
			}

		horizMarker += rowHeight;
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
	horizMarker = y+t->borders;
	for (yy = 0; yy < t->numRows; yy++) {
		vertMarker = x+(t->borders/2);
		rowHeight = field->rowHeight;
		for (xx = 0; xx < t->numColumns; xx++) {
			colWidth = field->colWidth;
			TableGetExpandedDimensions(t,xx,yy,
				&expandedWidth,&expandedHeight);
			if ((ex >= vertMarker)&&
			    (ex < (vertMarker + expandedWidth))&&
			    (ey >= horizMarker)&&
			    (ey < (horizMarker + expandedHeight))) {
				if (field->type == F_TABLE) {
					return(TableAnchorAt(
						(TableInfo *)field->table,
						vertMarker+FIELD_BORDER_SPACE,
						horizMarker+FIELD_BORDER_SPACE,
						ex, ey));
					}
				return(field->href);
				}
			vertMarker += colWidth;
			field++;
			}
		horizMarker += rowHeight;
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
