/*	RCSLOAD.C	7/3/85		Tim Oren		*/

/*************************************************************
 * Copyright 1999 by Caldera Thin Clients, Inc.              *
 * This software is licensed under the GNU Public License.   *
 * Please see LICENSE.TXT for further information.           *
 *************************************************************/
#include <gem.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "rcsdefs.h"
#include "rcs.h"
#include "rcsdata.h"
#include "rcslib.h"
#include "rcsvdi.h"
#include "rcsmain.h"
#include "rcsfiles.h"
#include "rcsobjs.h"
#include "rcsintf.h"
#include "rcsload.h"

#define CR 0x0D
#define LF 0x0A
#define CTRLZ 0x1A
#define ZERO '0'
#define SMX 'x'
#define SPACE ' '
#define COMMA ','
#define OPENBR '{'
#define CLOSEBR '}'
#define SEMICOLON ';'

/*	this module contains the file reading & writing routines */
/*	for ICONEDIT. */

/* The icon file format is: */
/* GEM Icon Definition: */
/*#define ICON_W 0x0000 */
/*#define ICON_H 0x0000 */
/*#define DATA_SIZE 0x0000 */
/*UWORD iconname[DATA_SIZE] = */
/*{ 0x8611, 0x482C, 0x8611, 0x8826, */
/*  0xFF1F, 0xF8FF, 0x0000, 0x0000, */
/*  ... */
/*  0xA106, 0xFF00, 0x0000, 0x0000 */
/*}; */
/*NOTE: All icon files have the extension .icn */
/*NOTE: As of 01/24/85, the written form of an icon has changed. Icons in */
/*the new format have a comment as their first line. Old icons do not. */
/*READ_ICON determines which type of icon is being read; WRIT_ICON only */
/*writes in the new form. */
/*In the written form, both mask & data are inverted and have their bytes */
/*swapped. */

void just_name(char *pname)
{
	/* pull the filename off the end of the path */
	char		*ptmp;

	ptmp = &icn_file[0];
	while(*ptmp)
		ptmp++;
	while(*ptmp != '\\')
		ptmp--;
	ptmp++;
	strcpy(pname, ptmp);
	while(*pname)
	{
		/* down-shift if necessary	*/
		if (*pname >= 'A' && *pname <= 'Z')
			*pname += 32;
		pname++;
	} /* while */
} /* just_name */

char dig_to_char(short number)
{
	char		b_val, result;

	b_val = (char) number;
	result = b_val + 48;
	if ( (result > 57) && (result < 65) )
		result += 7;
	return(result);
} /* dig_to_char */

short writ_icon(long praster, short icon_w, short icon_h, short data_size, short data_field)
{
	short	count;
	short	ii, jj;
	short	number, factor, digit;
	char	chr, new_name[13];
	char	*ptr;
	FILE	*f_handle;

	if (data_field)
		f_handle = fopen(icn_file, "w");
	else
	{
		f_handle = fopen(icn_file, "r+");
		if (f_handle != NULL)
			fseek( f_handle, 0, SEEK_END);  /* go to end of file */
	}
	if (f_handle == NULL)
		return(false);
	else
	{
		new_name[0] = '\0';
		just_name(&new_name[0]);
		for (ii = 0; ii < MAPSIZE; c_obndx[ii++] = '\0')
			;
		ptr = (char *) &c_obndx[0];

		ptr = strcat(ptr, "/* GEM Icon Definition: */");
		ptr--;
		*ptr++ = CR;
		*ptr++ = LF;
		ptr = strcat(ptr, "#define ICON_W 0x");
		ptr--;
		factor = 12;
		for (jj = 1; jj <= 4; jj++)
		{  
			digit = (icon_w >> factor) & 0x000F;
			chr = dig_to_char(digit);
			*ptr++ = chr;
			factor -= 4;
		} /* for */
		*ptr++ = CR;
		*ptr++ = LF;

		ptr = strcat(ptr, "#define ICON_H 0x");
		ptr--;
		factor = 12;
		for (jj = 1; jj <= 4; jj++)
		{  
			digit = (icon_h >> factor) & 0x000F;
			chr = dig_to_char(digit);
			*ptr++ = chr;
			factor -= 4;
		} /* for */
		*ptr++ = CR;
		*ptr++ = LF;

		ptr = strcat(ptr, "#define DATASIZE 0x");
		ptr--;
		factor = 12;
		for (jj = 1; jj <= 4; jj++)
		{  
			digit = (data_size >> factor) & 0x000F;
			chr = dig_to_char(digit);
			*ptr++ = chr;
			factor -= 4;
		} /* for */
		*ptr++ = CR;
		*ptr++ = LF;

		ptr = strcat(ptr, "UWORD ");
		ptr--;
		ii = 0;
		while (new_name[ii] != '.')
			*ptr++ = new_name[ii++];
		ptr = strcat(ptr, "[DATASIZE] =");
		ptr--;
		*ptr++ = CR;
		*ptr++ = LF;
		ptr = strcat(ptr, "{ ");
		ptr--;
		count = 0;

		ii = (data_size * 2) / icon_h;
		gsx_untrans(praster, ii, praster, ii, icon_h);

		for (ii = 0; ii < data_size; ii++)
		{

			*ptr++ = ZERO;	/* '0' */
			*ptr++ = SMX;	/* 'x' */

			number = (*(short *)(praster + 2 * ii));
			/* now turn it into ASCII	*/
			factor = 12;
			for (jj = 1; jj <= 4; jj++)
			{
				digit = (number >> factor) & 0x000F;
				chr = dig_to_char(digit);
				*ptr++ = chr;
				factor -= 4;
			} /* for */

			*ptr++ = COMMA;
			*ptr++ = SPACE;
			count++;
			if ( (count % 4) == 0 )
			{
				*ptr++ = CR;
				*ptr++ = LF;
				ptr = strcat(ptr, "  ");
				ptr--;
			} /* if count */
		} /* for ii */

		ii = (data_size * 2 ) / icon_h;
		gsx_trans(praster, ii, praster, ii, icon_h);

		ptr -= 6;
		*ptr++ = CR;
		*ptr++ = LF;
		*ptr++ = CLOSEBR;
		*ptr++ = SEMICOLON;
		*ptr++ = CR;
		*ptr++ = LF;
		*ptr = '\0';

		fputs((char *)c_obndx, f_handle);
		fclose(f_handle);
		return(true);
	}
} /* writ _icon */

void get_icnfile(char *full_path, short label)
{
	short		butn, ii;
	char		tmp_name[13];

	if ( !sav_icnpath[0] )
		get_path(full_path, "*.ICN"); 
	else
		strcpy(full_path, sav_icnpath);
	tmp_name[0] = '\0';
	if (Sversion() >= 0x0140)
	{
		long	fs_label;

		fs_label = string_addr(label);
		fsel_exinput(full_path, &tmp_name[0], &butn, (char  *)fs_label);
	}
	else
		fsel_input(full_path, &tmp_name[0], &butn);
	if (butn)
	{
		if ( strcmp( sav_icnpath, full_path ) == 0)
		{
			strcpy(sav_icnpath, full_path);
			ii = 0;
			while(full_path[ii] != '*')
				ii++;
			while( full_path[ii] != '\\' )
				ii--;		
			full_path[ii] = '\0';
			Dsetpath( ((long)(full_path)));
			strcat( full_path, "\\" );
		}
		else
		{
			ii = 0;
			while(full_path[ii] != '*')
				ii++;	    
			while(full_path[ii] != '\\')
				ii--;	
			full_path[++ii] = '\0';	      
		}
		strcat(full_path, &tmp_name[0]);
	} /* if butn */
	else
		full_path[0] = '\0';
} /* get_icnfile */

short char_to_dig(char chr)
{
	short		result;

	result = chr - 48;
	if (result > 9)
		result -= 7;
	return( (short) result );
} /* char_to_dig */

void get_number(char *ptr, short *result)
{
	short		ii, factor, digit;
	char		chr;

	factor = 12;
	*result = 0x0;
	for (ii = 1; ii <= 4; ii++)
	{  
		chr = *ptr++;
		if (chr == 0x0D)
			return;
		digit = char_to_dig(chr);
		*result |= digit << factor;
		factor -= 4;
	} /* for */
} /* get_number */

long read_icon(char *buffer, short *icon_w, short *icon_h, short *data_size, char *icon_name, short mask, short *icn_size)
{
	long	praster;
	short	count, ii, jj,  bytes_read, new_style;
	FILE	*f_handle;
	short	number, factor, digit;
	char	chr, *ptr;

	f_handle = fopen(&icon_name[0], "rb");
	if (f_handle == NULL)
	{
		*icon_w = -1;
		return(false);
	} /* if */
	else
	{
		if ( !mask  || *icn_size == -1 )
			bytes_read = dmcopy(f_handle,0x0L,(long)((long)(buffer)),MAPSIZE);
		else
			bytes_read = dmcopy(f_handle, (long) *icn_size, (long)((long)(buffer)),MAPSIZE);
		if (bytes_read <= 0)
		{
			*icon_w = -1;
			*icn_size = -1;	
			return(false);      
		} /* if */
		else
		{
			ptr = buffer;	/* initialize pointers */

			/* is this an new-style icon?	*/
			if (*ptr == '/')
			{
				new_style = true;
				/* skip the comment	*/
				while (*ptr != '#')
				ptr++;
			} /* if */
			else
				new_style = false;
			/* get ICON_W */
			while (*ptr != '0')
				ptr++;
			ptr += 2;
			get_number(ptr, icon_w);
			ptr += 4;
			/* get ICON_H */
			while (*ptr != '0')
				ptr++;
			ptr += 2;
			get_number(ptr, icon_h);
			ptr += 4;
			/* get DATASIZE */
			while (*ptr != '0')
				ptr++;
			ptr += 2;
			get_number(ptr, data_size);		/* pick up DATA_SIZE	*/

			praster = get_mem(*data_size * 2);
			/* skip "UWORD filename..."	*/
			while (*ptr != OPENBR)
				ptr++;
			ptr += 2;	/* skip '{ '		*/

			count = 0;
			for (ii = 0; ii < *data_size; ii++)
			{
				ptr += 2;	/* skip "0x"	*/
				factor = 12;
				number = 0x0;
				for (jj = 1; jj <= 4; jj++)
				{  
					chr = *ptr++;
					digit = char_to_dig(chr);
					number |= digit << factor;
					factor -= 4;
				} /* for */

				(*(short *)(praster + ii * sizeof(unsigned short)) = (number));
				ptr += 2;	/* skip ", " */
				count++;
				if ( (count % 4) == 0 )
					ptr += 4;	/* skip "CRLF  " */
			} /* for ii */
			if (new_style)
			{
				ii = (*data_size * 2) / *icon_h;
				gsx_trans(praster, ii, praster, ii, *icon_h);
			}
			*icn_size = (short)(ptr - buffer);
			fclose(f_handle);
			return (praster);
		}
	}
} /* read_icon */

short bb_get(long taddr, short *icon_w, short *icon_h)
{
	long	where;
	short	icon_size, file_size;

	get_icnfile(&icn_file[0], STLDICN);
	file_size = -1;
	if (icn_file[0])
	{
		where = read_icon((char *) &c_obndx[0], icon_w, icon_h, 
			&icon_size, &icn_file[0], true ,&file_size );
		if (where)
		{
			(*(long *)(BI_PDATA(taddr)) = (where));
			(*(short *)(BI_HL(taddr)) = (*icon_h));
			(*(short *)(BI_WB(taddr)) = (2 * icon_size / *icon_h));
			icn_state = FILE_STATE;
			return (true);
		}
	}
	return(false);
}

short ib_get(long taddr, short *icon_w, short *icon_h, short maskp, short *file_size)
{
	long	where;
	short	icon_size;
/*	BYTE	mask_file[80];	*/

	if(*file_size == -1 && !maskp)  /* Need to get file name */
/*		if ( maskp )
			get_icnfile(&mask_file[0]);
		else
*/
	{
		get_icnfile(&icn_file[0], STLDICN);
		*file_size = 0;
	}
	if (!icn_file[0])
		return(false);
	else
	{
		where = 0x0L;
		if (icn_file[0])
			where = read_icon((char *) &c_obndx[0], icon_w, icon_h, 
				&icon_size, &icn_file[0], maskp, file_size);
/*		else
			if(mask_file[0] && maskp && *file_size == -1)
				where = read_icon((BYTE *) &c_obndx[0], icon_w,
					icon_h, &icon_size, &mask_file[0], 
					TRUE, file_size);
*/		if (where)
		{
			if (!maskp)
				(*(long *)(IB_PDATA(taddr)) = (where));
			else
				(*(long *)(IB_PMASK(taddr)) = (where));
			(*(short *)(IB_HICON(taddr)) = (*icon_h));
			(*(short *)(IB_WICON(taddr)) = (*icon_w));
			icn_state = FILE_STATE;
			return (true);
		}
		else
			return (false);
	}
}

void load_part(short obj)
{
	long	tree, taddr, traddr ;
	GRECT	p;
	short	type, sizeit,  icn_fsize, icn_w, icn_h;

	if (iconedit_flag)
		traddr = save_tree;
	else
		traddr = ad_view;
	obj_redraw(traddr, obj);
	taddr = GET_SPEC(traddr, obj);
	type = ((GET_TYPE(traddr, obj)) & 0xff);
	if (type == G_IMAGE)
	{
		if (bb_get(taddr, &p.g_w, &p.g_h))
			newsize_obj(traddr, obj, p.g_w, p.g_h, false);
		if ( (((GET_TYPE(traddr, obj)) >> 8) & 0xff))
		{
			tree = traddr;
			(*(short *)(OB_TYPE(obj)) = ((G_IMAGE & 0x00ff)));
		}
	}
	else
	{
		sizeit = false;

		icn_fsize = -1;  /* display fsel */
		if (ib_get(taddr, &p.g_w, &p.g_h, false, &icn_fsize))
			sizeit = true;
		else if(!icn_file[0])	/*cancel button in file sel*/
			return;
		icn_w = p.g_w;
		icn_h = p.g_h;
		if (ib_get(taddr, &p.g_w, &p.g_h, true, &icn_fsize))
			sizeit = true;				  
		if (icn_fsize <= 0 )	      
		{
			icn_fsize = -1; /* display fsel to get mask */
			if (ib_get(taddr, &p.g_w, &p.g_h, true, &icn_fsize))
				sizeit = true;			       
		}     
		if (icn_w != p.g_w || icn_h != p.g_h)
		{
			hndl_alert(1,string_addr(STSIZEP));
			sizeit = false;
			p.g_w = -1;
		}
		rcs_edited = true;
		if (sizeit)
		{
			icon_tfix(taddr);
			icon_wh(taddr, &p.g_w, &p.g_h);
			newsize_obj(traddr, obj, p.g_w, p.g_h, false);
		}
		if ( (((GET_TYPE(traddr, obj)) >> 8) & 0xff))
		{
			tree = traddr;
			(*(short *)(OB_TYPE(obj)) = ((G_ICON & 0x00ff)));
		}
	}
	obj_redraw(traddr, obj); 
}
