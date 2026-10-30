/*************************************************************
 * Copyright 1999 by Caldera Thin Clients, Inc.              *
 * This software is licensed under the GNU Public License.   *
 * Please see LICENSE.TXT for further information.           *
 *************************************************************/
#include <gem.h>
#include <string.h>
#include <stdbool.h>
#include "rcs.h"
#include "rcslib.h"
#include "rcsfiles.h"
#include "rcsload.h"
#include "rcsiext.h"
#include "rcsiinit.h"
#include "rcsiedit.h"
#include "rcsimain.h"
#include "rcsifile.h"

void clear_img(void)
{
	short old_color, old_pen;
	
	old_color = colour;
	old_pen = pen_on;
	colour = WHITE;
	pen_on = ERASEBOX;
	solid_img();
	fb_redraw();
	colour = old_color;
	pen_on = old_pen;
}

short save_icn(void)
{    
	long	daddr, maddr;
	short	ret;
					    
	save_fat(false);
	rast_op(3,&hold_area, &hold_mfdb, &hold_area, &save_mfdb);
	if(gl_isicon)
		rast_op(3, &hold_area, &hld2_mfdb, &hold_area, &sav2_mfdb);
	daddr = GET_SPEC(ad_pbx, DATAWIND); 
	if (gl_isicon)
	{
		/* write data and mask */
		maddr = GET_SPEC(ad_pbx, MASKWIND);
		ret = writ_icon((*(long *)(BI_PDATA(daddr))),
			gl_wimage, gl_himage, gl_datasize,
			true) ;
		if (ret )
			return( writ_icon((*(long *)(BI_PDATA(maddr))),
			gl_wimage, gl_himage, gl_datasize,
			false) );
		else 
			return( ret );
	}
	else	 
		/* write data only */
		return(	writ_icon((*(long *)(BI_PDATA(daddr))),
			gl_wimage, gl_himage, gl_datasize,
			true) );
}

short svas_icn(void)
{
	get_icnfile(&icn_file[0], STSASICN);
	if (!icn_file[0])
		return(false);
	else		  
		if (save_icn())
		{
			icn_state = FILE_STATE;
			return(true);
		}
		else
			return(false);	
}   

short new_img(void)
{
	short	abtn;					  
	if ( icn_edited && !rcs_xpert )
	{
	 	if ( (abtn = abandon_button()) == 1 ) 
   		{
			clear_img();				    
			return( true );
		}
		else if (abtn == 2) /* Save Changes */			   
		{
			if ( icn_state == NOFILE_STATE)
				return( svas_icn());
			else
				return(	save_icn());
		}
		else /* cancel */
			return (false);
	}
	else			      
	{
		clear_img();
		return( true );
	}
}

void do_open(void)
{
    short state;
	load_part(save_obj); /*if successfully load, icn_state = file_state*/
	state = icn_state;
	icn_init(false);
	icn_edited = false;
	icn_state = state;	/*icn_init will put icn_state = nofile_state*/
}

short icnopen(void)
{
	short	abtn;					  

	if ( icn_edited && !rcs_xpert )
	 	if ( (abtn = abandon_button()) == 1 ) 
   		{
			do_open();
			return( true );
		}
		else if (abtn == 2) /* Save Changes */			   
		{
			if ( icn_state == NOFILE_STATE)
				if( svas_icn())
				{
					do_open();
					return(true);
				}
				else
					return( false );
			else
				if(save_icn())
				{
					do_open();
					return(true);
				}
				else
					return( false );
	
		}
		else /* cancel */
			return (false) ;
	else			      
	{
		do_open();
		return( true );
	}
}

short rvrt_icn(void)
{
	short	abtn;					  

	if ( icn_edited && !rcs_xpert )
	 	if ( (abtn = abandon_button()) == 1 ) 
   		{
			undo_img();				    
			return( true );
		}
		else if (abtn == 2) /* Save Changes */			   
			if ( icn_state == NOFILE_STATE)
				if( svas_icn())
					return(true);
				else
					return(false);
			else
				if(save_icn())
					return(true);
				else
					return(false);
		else /* cancel */
			return (false) ;
	else			      
	{
		undo_img();
		return( true );
	}
}
