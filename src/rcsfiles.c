/*	RCSFILES.C	10/08/84 - 1/25/85	Tim Oren		*/
/*************************************************************
 * Copyright 1999 by Caldera Thin Clients, Inc.              *
 * This software is licensed under the GNU Public License.   *
 * Please see LICENSE.TXT for further information.           *
 *************************************************************/
#include <gem.h>
#include <stdbool.h>
#include <string.h>
#include "rcsdefs.h"
#include "rcs.h"
#include "rcsdata.h"
#include "rcsinit.h"
#include "rcsintf.h"
#include "rcstrees.h"
#include "rcswrite.h"
#include "rcsread.h"
#include "rcsfiles.h"

void get_path(char *tmp_path, char *spec)
{
	short	cur_drv;

	cur_drv = Dgetdrv();
	tmp_path[0] = cur_drv + 'A';
	tmp_path[1] = ':';
	tmp_path[2] = '\\';
	Dgetpath((char *)((long)(&tmp_path[3])), cur_drv+1);
	if (strlen(tmp_path) > 3)
		strcat(tmp_path, "\\");
	else
		tmp_path[2] = '\0';
	strcat(tmp_path, spec);
}

void r_to_xfile(char *where, char *ext)
{
	short	ii, jj;

	strcpy(where, rcs_rfile);
   	ii = (jj = (short)strlen(where)) - 1 ; 
	while ((ii && where[ii] != '.') && where[ii] != '\\')
		ii--;
	if (where[ii] != '.')
		where[ii = jj] = '.';
	strcpy(&where[++ii], ext);
}

short get_file(short label)
{
	short	butn, ii;
	char	tmp_path[64], tmp_name[13];
		
	if ( !sav_rcspath[0] )
		get_defpath(tmp_path, "*.RSC");
	else
		strcpy(tmp_path, sav_rcspath);
	tmp_name[0] = '\0';

	if (Sversion() >= 0x0140)
	{
		long	fs_label;

		fs_label = string_addr(label);
		fsel_exinput(&tmp_path[0], &tmp_name[0], &butn, (char  *)fs_label);
	}
	else
		fsel_input(&tmp_path[0], &tmp_name[0], &butn);
	graf_mouse(rcs_mform, 0x0L);
	if (butn)
	{
		if (!tmp_name[0])
			if (hndl_alert(1, string_addr(NOFNAME)) == 1)
				return(false);
		if (strcmp(sav_rcspath, tmp_path))
		{
			Dsetpath((const char *)((long)(tmp_path)));
			strcpy(sav_rcspath, tmp_path);
		}
		strcpy(rcs_rfile, &tmp_path[0]);
	 	for (ii = 0; rcs_rfile[ii] && rcs_rfile[ii] != '*'; ii++)
	 		;
	  	rcs_rfile[ii - 1] = '\0';	/* knock off slash */
		strcat(rcs_rfile, "\\");
		strcpy(&rsc_path[1], rcs_rfile);
		rsc_path[0] = strlen(rcs_rfile);
	  	strcat(rcs_rfile, &tmp_name[0]);
		/* Force 'rsc' extension on resource file.*/
			r_to_xfile(rcs_rfile, "RSC"); 
		r_to_xfile(rcs_dfile, "DFN");
		return (true);
	} /* if butn */
	else
		return (false);
} /* get_file */

short rvrt_files(void)
{
	if (!open_files(true))
		return(false);
	else if (!read_files())
		return(false);
	else
	{
		redo_trees();
		return(true);
	}	
}

short ok_rvrt_files(void)   /* kluge routine to allow calls between sibling overlays */
{
	if (rvrt_files() )
	{
		new_state( FILE_STATE );
		rsc_title(); 
	}
	else
		view_trees();
	return( cont_rcsinit(false)); /* return to rcs_init in overlay */		      
}

void rsc_title(void)
{
   	set_title(&rcs_rfile[0]);
}

static void clr_rsc(void)
{
	ini_buff();
	rcs_ndxno = 0;
	new_state(NOFILE_STATE);
	clr_title();
	rcs_trpan = 0;
	redo_trees();
}

short abandon_button(void)
{		
	return(  hndl_alert(1,string_addr(ABNDCHGS)) );
}

static void cls_rsc(void)
{
	mouse_form(HGLASS);
	if (write_files())
		clr_rsc();
	mouse_form(ARROW);
}

short new_rsc(void)
{
	short	abtn;
	if ( rcs_edited && !rcs_xpert )
	{
	 	if ( (abtn = abandon_button()) == 1 ) 
   		{
			clr_rsc();				    
			return( true );
		}
		else if (abtn == 2) /* Save Changes */			   
		{
			if ( rcs_state == NOFILE_STATE)
				if (!get_file(STSASRES))
					return(false);
			cls_rsc();
			clr_rsc();
			return(true);
		}
		else /* cancel */
			return (false) ;
	}
	else			      
	{
		clr_rsc();
		return( false );
	}
}

static short opn_rsc(void)
{
	mouse_form(HGLASS);
	if (open_files(false))
	{
		if (read_files())
		{  
			new_state(FILE_STATE);
			rsc_title();
		}
	}
	else 
	{
		mouse_form(ARROW);
		return(false);	   
	}
	rcs_trpan = 0;
	redo_trees();
	mouse_form(ARROW);
	return(true);
}

short open_rsc(void)
{
	short	abtn;
	if (rcs_edited && !rcs_xpert)
	{
		if ((abtn = abandon_button()) == 1)
			return(opn_rsc());
		else if (abtn == 2)
		{	  
			if (rcs_state == NOFILE_STATE)
				if( ! get_file(STSASRES) )
					return(false);
			cls_rsc();
			return(opn_rsc());
		}
		else
			return(false);
	}
	else			     
		 return (opn_rsc());
}

short clos_rsc(void)
{
	short	abtn;
	if (rcs_edited && !rcs_xpert)
	{
		if ((abtn = abandon_button()) == 1)
		{
			clr_rsc();
			return(true);
		}
		else if (abtn == 2)
		{	  
			if ( rcs_state == NOFILE_STATE)
				if (!get_file(STSASRES))
					return(false);
			cls_rsc();
			return(true);
		}
		else
			return(false);
	}
	else			     
	{
		clr_rsc();
		return(true);
	}
}

void save_rsc(void)
{
	mouse_form(HGLASS);
	write_files();
	rvrt_files();
	mouse_form(ARROW);
}

short svas_rsc(void)
{
	if (!get_file(STSASRES))
		return(false);
	else
	{
		mouse_form(HGLASS);
		write_files();
		rvrt_files();
		mouse_form(ARROW);
		new_state(FILE_STATE);
		rsc_title();	      
		return(true);
	}
}

short rvrt_rsc(void)
{
	short	abtn;
	if (rcs_edited && !rcs_xpert)
	{
		if ((abtn = abandon_button()) == 1)
		{
			mouse_form(HGLASS);
			if ( rcs_state == NOFILE_STATE)
				clr_rsc();
			else	 
				rvrt_files();
			mouse_form(ARROW);
			return(true);
		}
		else if (abtn == 2)	     
		{
			if( rcs_state == NOFILE_STATE)
				if( ! get_file(STSASRES) )
					return(false);
			cls_rsc();
			return(true);
		}
		else
			return(false);
	}
	else
	{
		mouse_form(HGLASS);
		rvrt_files();
		mouse_form(ARROW);   
		return(false);
	}	
}

void merge_rsc(void)
{
	char	sv_rfile[80], sv_dfile[80];

	strcpy(&sv_rfile[0], rcs_rfile);
	strcpy(&sv_dfile[0], rcs_dfile);
	mouse_form(HGLASS);
	if (open_files(false))
		if (merge_files())
			redo_trees();
	if (rcs_state == FILE_STATE)
	{
		strcpy(rcs_rfile, &sv_rfile[0]);
		strcpy(rcs_dfile, &sv_dfile[0]);
	}
	mouse_form(ARROW);
}
	
short quit_rsc(void)
{
	short	abtn;
	if ( rcs_edited && !rcs_xpert )
	{
	 	if ( (abtn = abandon_button()) == 1 ) 
    			return(true);
		else if (abtn == 2) /* Save Changes */			   
		{
			if( rcs_state == NOFILE_STATE)
				if( ! get_file(STSASRES) )
					return(false);
			cls_rsc();
			return(true);
		}
		else /* cancel */
			return (false);
	}
	else
		return(true);
}
