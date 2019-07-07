/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2005 Kannel Group  
 * Copyright (c) 1998-2001 WapIT Ltd.   
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 * 
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 * 
 * 2. Redistributions in binary form must reproduce the above copyright 
 *    notice, this list of conditions and the following disclaimer in 
 *    the documentation and/or other materials provided with the 
 *    distribution. 
 * 
 * 3. The end-user documentation included with the redistribution, 
 *    if any, must include the following acknowledgment: 
 *       "This product includes software developed by the 
 *        Kannel Group (http://www.kannel.org/)." 
 *    Alternately, this acknowledgment may appear in the software itself, 
 *    if and wherever such third-party acknowledgments normally appear. 
 * 
 * 4. The names "Kannel" and "Kannel Group" must not be used to 
 *    endorse or promote products derived from this software without 
 *    prior written permission. For written permission, please  
 *    contact org@kannel.org. 
 * 
 * 5. Products derived from this software may not be called "Kannel", 
 *    nor may "Kannel" appear in their name, without prior written 
 *    permission of the Kannel Group. 
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES 
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
 * DISCLAIMED.  IN NO EVENT SHALL THE KANNEL GROUP OR ITS CONTRIBUTORS 
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,  
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT  
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR  
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,  
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE  
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,  
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 * ==================================================================== 
 * 
 * This software consists of voluntary contributions made by many 
 * individuals on behalf of the Kannel Group.  For more information on  
 * the Kannel Group, please see <http://www.kannel.org/>. 
 * 
 * Portions of this software are based upon software originally written at  
 * WapIT Ltd., Helsinki, Finland for the Kannel project.  
 */ 

/*
 * accesslog.c - implement access logging functions
 *
 * see accesslog.h.
 *
 * Kalle Marjola 2000 for Project Kannel
 */


#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <dirent.h>
#include <time.h>
#include <sys/time.h>



#include "gwlib.h"

static FILE *file = NULL;
static char filename[FILENAME_MAX + 1]; /* to allow re-open */
static int use_localtime;
static int markers = 1;     /* can be turned-off by 'access-log-clean = yes' */

//[sanchal][170309][ Variable used in accesslog_file_size]
static char gszFileName[FILENAME_MAX + 1];
//[sanchal][070309][ Conter For Appending in the log ]
static unsigned long counter = 0;



/*
 * Reopen/rotate lock.
 */
static List *writers = NULL;

void alog_reopen(void)
{
    if (file == NULL)
	return;

    if (markers)
        alog("Log ends");

    gwlist_lock(writers);
    /* wait for writers to complete */
    gwlist_consume(writers);

    fclose(file);
    file = fopen(filename, "a");

    gwlist_unlock(writers);

    if (file == NULL) {
        error(errno, "Couldn't re-open access logfile `%s'.", filename);
    } 
    else if (markers) {
        alog("Log begins");
    }
}


void alog_close(void)
{

    if (file != NULL) {
        if (markers)
            alog("Log ends");
        gwlist_lock(writers);
        /* wait for writers to complete */
        gwlist_consume(writers);
        fclose(file);
        file = NULL;
        gwlist_unlock(writers);
        gwlist_destroy(writers, NULL);
        writers = NULL;
    }
}


void alog_open(char *fname, int use_localtm, int use_markers)
{
    FILE *f;
    
    use_localtime = use_localtm;
    markers = use_markers;

    if (file != NULL) {
        warning(0, "Opening an already opened access log");
        alog_close();
    }
    if (strlen(fname) > FILENAME_MAX) {
        error(0, "Access Log filename too long: `%s', cannot open.", fname);
        return;
    }

    if (writers == NULL)
        writers = gwlist_create();

    f = fopen(fname, "a");
    if (f == NULL) {
        error(errno, "Couldn't open logfile `%s'.", fname);
        return;
    }
    file = f;
    strcpy(filename, fname);
    info(0, "Started access logfile `%s' :%d.", filename,markers);
    if (markers)
        alog("Log begins");
	
}

Octstr *change_access_file_name(Octstr *accesslog_name)
{
	/*
	Octstr *new_name;
	Octstr *original_name;
	Octstr *remaining_name;
	Octstr *log_name;
	
	int index,i;
	
	FILE *new_fp;
	
	struct tm ptr;
	time_t tm;
	char time_string[20];
	
	alog("Log ends");
	if(old_name == NULL)
		return NULL;
		
	new_name = remaining_name = original_name = NULL;
	
	index = octstr_search_char(old_name,'.',0);
	if(index != -1)
	{
		remaining_name = octstr_copy(old_name,index,octstr_len(old_name));			
	}
	else
		index = octstr_len(old_name);
		
	log_name = octstr_copy(old_name,0,index);
	//this is cos _yyyy_mm_dd is 11 chars
	original_name = octstr_copy(log_name,0,((octstr_len(log_name))-11));
	
		
	tm = time(NULL);
	ptr = gw_localtime(tm);
	gw_strftime(time_string,20,"_%Y_%m_%d",&ptr);
	new_name = octstr_copy(original_name,0,octstr_len(original_name));
	octstr_append_data(new_name,time_string,strlen(time_string));
	if(remaining_name != NULL)
		octstr_append(new_name,remaining_name);
	
	//info(0,"1.old_name : %s",octstr_get_cstr(old_name));
	//info(0,"2.log_name : %s",octstr_get_cstr(log_name));
	//info(0,"3.remaining_name : %s",octstr_get_cstr(remaining_name));
	//info(0,"4.original_name : %s",octstr_get_cstr(original_name));
	
	info(0,"5.new_name : %s",octstr_get_cstr(new_name));
	
	new_fp = fopen(octstr_get_cstr(new_name), "a");
	if(new_fp == NULL) 
	{
    	error(errno, "Couldn't open access logfile `%s'.", octstr_get_cstr(new_name));
    	return NULL;
	}

	gwlist_lock(writers);
    fclose(file);
	file = new_fp;
    strcpy(filename, octstr_get_cstr(new_name));
	
	gwlist_unlock(writers);
	 
    info(0, "Started access logfile `%s'.", filename);
    if (markers)
        alog("Log begins");
	return new_name;
	*/

	int index,i;
    struct tm ptr;
	time_t tm;
	char time_string[20];
	char extensn_string[20];

	Octstr *log = NULL;
	Octstr *new_name = NULL;
	Octstr *original_name = NULL;
	Octstr *remaining_name = NULL;
	Octstr *log_name = NULL;

	//[sanchal[290709][Code Added from here]
	int iTemp=0;
    int iBytesToBeCrtd=0;
	char *szTemp=NULL;
	char *szStr=NULL;
	char *chTemp=NULL;
	//[sanchal[290709][Code Added till here]

	FILE *new_fp;
	index = i = 0;

	if(writers == NULL)
		return NULL;

	if (accesslog_name != NULL )
	      ;
	else
	      return NULL;

	//[sanchal[290709][Code Added from here]
	iBytesToBeCrtd = octstr_len(accesslog_name);
	szStr = gw_malloc(iBytesToBeCrtd + 1);
	szTemp = gw_malloc(iBytesToBeCrtd + 1);
	//[sanchal[290709][Code Added till here]
	
	//[sanchal][220109][As New days file is create ,so, the counter for it]
  	//[is initialized to zero for accesslog                               ]
	counter = 0;

	//[sanchal][121208]
	index = octstr_search_char(accesslog_name,'.',0);
	if(index != -1)
	{
		remaining_name = octstr_copy(accesslog_name,index,(octstr_len(accesslog_name)));
		//gets the .log string
	}
	else
		index = octstr_len(accesslog_name);

	log_name = octstr_copy(accesslog_name,0,index);
    //this is cos _yyyy_mm_dd is 11 chars

    //[sanchal][280709][Code Added from here]
    strcpy( szStr, octstr_get_cstr(log_name));
	strcpy(szTemp, reversestring(szStr, strlen(szStr)-1));
	iTemp = 0;

	chTemp = strtok(szTemp,"-");
	if( chTemp != NULL )
	{
		if(strrchr(chTemp,'_') == NULL)
		// D_M_Y string is not present
		{
				memset(szStr,'\0',iBytesToBeCrtd+1);
				strcpy(szStr, chTemp);
				iTemp = strlen(szStr);
				iTemp += 1;
		}
	}
	else
	{
		memset(szStr,'\0',iBytesToBeCrtd+1);
		strcpy(szStr, chTemp);
	}

	memset(szStr,'\0',iBytesToBeCrtd+1);
	strcpy( szStr, octstr_get_cstr(log_name));
	//  ( stringsize - countersize )
	szStr[strlen(szStr)-(iTemp+11)]='\0';
    original_name=octstr_create(szStr);
    //[sanchal][280709][Code Added till here]

	//original_name = octstr_copy(log_name,0,(octstr_len(log_name))-11);

    tm = time(NULL);
	ptr = gw_localtime(tm);
	gw_strftime(time_string,20,"_%Y_%m_%d",&ptr);
	//gw_strftime(time_string,20,"_%H_%M",&ptr);
	
	new_name = octstr_copy(original_name,0,octstr_len(original_name));

	octstr_append_data(new_name,time_string,strlen(time_string));

	if(remaining_name != NULL)
		octstr_append(new_name,remaining_name);

    info(0,"1.accesss logfile name : %s",octstr_get_cstr(accesslog_name));
    info(0,"2.new_name : %s",octstr_get_cstr(new_name));

    new_fp = fopen(octstr_get_cstr(new_name), "a");
    if(new_fp == NULL)
    {
	    error(errno, "Couldn't open logfile `%s'.", octstr_get_cstr(new_name));
	    return -1;
	}
	else
	    alog("alog_end");
																		  
     //[sanchal][190109][When change in date happens]
	 //[then name should be passed on to accesslog_file_size]
	 //[ inorder to continue indexing for the same day]
	 memset(gszFileName,'\0',sizeof(gszFileName));
	 strcpy( gszFileName , octstr_get_cstr(new_name));

     gwlist_lock(writers);
     /*this is to prevent any other to wtite into the file until name has been changed.*/

	if(file != NULL)
		fclose(file);

	file = new_fp;

	strcpy(filename, octstr_get_cstr(new_name));

	gwlist_unlock(writers);

	alog ("alog_begins");

	info(0, "Access-log Added logfile UPDATED.");


    //[sanchal][280709][Code Added from here]
    gw_free (szStr);
	gw_free (szTemp);
	//[sanchal][280709][Code Added till here]

	octstr_destroy(log);
	octstr_destroy(original_name);
	octstr_destroy(remaining_name);
	octstr_destroy(log_name);

    return new_name;
}

void alog_use_localtime(void)
{
    use_localtime = 1;
}


void alog_use_gmtime(void)
{
    use_localtime = 0;
}


#define FORMAT_SIZE (10*1024)
static void format(char *buf, const char *fmt)
{
    time_t t;
    struct tm tm;
    char *p, prefix[1024];
	
    p = prefix;

    if (markers) {
        time(&t);
        if (use_localtime)
            tm = gw_localtime(t);
        else
            tm = gw_gmtime(t);

        sprintf(p, "%04d-%02d-%02d %02d:%02d:%02d ",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec);
    } else {
        *p = '\0';
    }

    if (strlen(prefix) + strlen(fmt) > FORMAT_SIZE / 2) {
        sprintf(buf, "%s <OUTPUT message too long>\n", prefix);
        return;
    }
    sprintf(buf, "%s%s\n", prefix, fmt);
}


/* XXX should we also log automatically into main log, too? */

void alog(const char *fmt, ...)
{
    char buf[FORMAT_SIZE + 1];
    va_list args;

    if (file == NULL)
        return;

    format(buf, fmt);
    va_start(args, fmt);

    gwlist_lock(writers);
    gwlist_add_producer(writers);
    gwlist_unlock(writers);

    vfprintf(file, buf, args);
    fflush(file);

    gwlist_remove_producer(writers);

    va_end(args);
}

//[sanchal] [170309][Changing access-log name by appending index when logsize is reached]
//It creates another file after checking the file size,
//if greater than specified in configuration file
int accesslog_file_size(long iLogSize, Octstr *accesslog_filename)
{
    char *szTemp, *szTemp1, *szTemp2, *szTempStr, *szTemp3, *szFileName;
    struct stat s;
    char *chTemp, *chTemp1;
    int iNoOfBytes, iCnt, index;
    long iBytesToBeAloctd;
    static int iFlag1 = 0;
    static int iTemp = 0;
    //static unsigned long counter = 0;
    FILE *new_fp;
    Octstr *new_name = NULL;
    Octstr *old_name = NULL;
    Octstr *original_name = NULL;
    Octstr *remaining_name = NULL;
    Octstr *log_name = NULL;

    iNoOfBytes = iCnt = index = iBytesToBeAloctd = 0;

	//debug("accesslog",0,"accesslog_file_size: Starting");
    //printf("Inside accesslog_file_size");

    if(accesslog_filename == NULL)
        return 1;

    iBytesToBeAloctd = octstr_len(accesslog_filename);
	szTemp = gw_malloc((iBytesToBeAloctd+1));
    szTemp1 = gw_malloc((iBytesToBeAloctd+1));
    szTemp2 = gw_malloc((iBytesToBeAloctd+1));
    szTemp3 = gw_malloc((iBytesToBeAloctd+1));
    szTempStr = gw_malloc((iBytesToBeAloctd+1));
    szFileName = gw_malloc((iBytesToBeAloctd+1));

    //[ NOTE --> 1st & 2nd condition will never be togather]
    //[ programme will be run for the day and counter      ]
    //[ will be incremented, so that, when the day changes ]
    //[ iTemp can never be zero                            ]
    if( iTemp == 0)
    {
        info(0, "iTemp is 0");
        strcpy( szFileName,octstr_get_cstr(accesslog_filename));
        strcpy( gszFileName, szFileName);
        iTemp++;
        //printf("accesslog_file_size: if szFileName : %s",szFileName);
		//debug("accesslog",0,"accesslog_file_size: Inside Zero condition");
    }
    else if( gszFileName != NULL )
        // If change in date happens it is reflected in
        // change_accesslogfile_name fn and name of the
        // created variable is to be carried to this fn
        // inorder to continue checking & indexing of file
        // for same date
        {
            strcpy( szFileName, gszFileName);
            //printf("accesslog_file_size:else if szFileName : %s",szFileName);
			//debug("accesslog",0,"accesslog_file_size: gszFileName : %s",gszFileName);
        }
    else
        {
            strcpy( szFileName, gszFileName);
            //printf("accesslog_file_size:else szFileName : %s",szFileName);
        }

    //printf("accesslog_file_size: szFileName : %s",szFileName);
	//debug("accesslog",0,"accesslog_file_size: szFileName : %s",szFileName);
	
	if(stat(szFileName,&s)<0)
    {
        gw_free(szTemp);
        gw_free(szTemp1);
        gw_free(szTemp2);
        gw_free(szTemp3);
        gw_free(szTempStr);
        gw_free(szFileName);
        return 1;
    }

    iNoOfBytes = s.st_size;

    //printf("\n accesslog_file_size: no of bytes : %ld iLogsize: %d \n",iNoOfBytes,iLogSize);
	//debug("accesslog",0,"accesslog_file_size: no of bytes : %ld iLogsize: %d ",iNoOfBytes,iLogSize);
	

    if ( iNoOfBytes >= (iLogSize * 1024 * 1024))
    {
        //info(0,"accesslog_file_size: Number of bytes : %ld",iNoOfBytes);
        //printf("\n accesslog_file_size: Number of bytes : %ld \n",iNoOfBytes);

        old_name = octstr_create(szFileName);

        // If the file have s.log
        index = octstr_search_char(old_name,'.',0);
        if(index != -1)
        {
              remaining_name = octstr_copy(old_name,index,(octstr_len(old_name)));
              //gets the .log string
              //log_name = octstr_copy(old_name,0,index);
              strcpy(szTemp,octstr_get_cstr(old_name));

              //remove .log file extension
              chTemp = strtok(szTemp ,".");
              strcpy( szTempStr, chTemp);
				memset(szTemp3,'\0',iBytesToBeAloctd+1);
              strcpy( szTemp3, chTemp);

              // s-1_D-1.log ( remove -1 )
              // Reverse the string
              // go till - & increment the pointer
              // Reverse the string

              memset(szTemp1,'\0',iBytesToBeAloctd+1);
              chTemp = NULL;
              strcpy(szTemp1, reversestring(szTempStr, strlen(szTempStr)-1));
              strcpy( szTemp2, szTemp1);

              chTemp1 = strtok(szTemp1, "-");
              if( chTemp1 != NULL )
              {
                   if((chTemp = strrchr(chTemp1,'_'))!=NULL)
                   {
                        memset(szTemp2,'\0',iBytesToBeAloctd+1);
                        strcpy(szTemp2, szTemp3);
                        log_name = octstr_create(szTemp2);
                   }
                else
                   {
                        memset(szTemp,'\0',iBytesToBeAloctd+1);
                        strcpy(szTemp, reversestring(szTemp1, strlen(szTemp1)-1));
                        counter = atoi(szTemp);

                        //Get the length of string till - in reverse order
                        while( szTemp2[iCnt] != '-' )
                             iCnt++;

                        memset(szTemp,'\0',iBytesToBeAloctd+1);
                        strcpy(szTemp, reversestring(szTemp2, strlen(szTemp2)-1));
                        szTemp[ strlen(szTemp)- iCnt - 1 ] = '\0';
                        log_name = octstr_create(szTemp);
                    }
                }
				else
                {
                memset(szTemp,'\0',iBytesToBeAloctd+1);
                strcpy(szTemp, szTempStr);
                log_name = octstr_create(szTemp);
                }

        }// end if checking index
        else
        {
            // s-1_D-1 ( remove -1 )
            // Reverse the string
            // go till - & increment the pointer
            // Reverse the string

            //memset(szTemp,'\0',sizeof(szTemp));
            //memset(szTemp1,'\0',sizeof(szTemp1));
            //memset(szTemp2,'\0',sizeof(szTemp2));
            //memset(szTempStr,'\0',sizeof(szTempStr));

            strcpy(szTempStr,octstr_get_cstr(old_name));
            strcpy(szTemp, reversestring(szTempStr, strlen(szTempStr)-1));
            strcpy( szTemp1, szTemp);
            chTemp = NULL;
            chTemp1 = NULL;

            chTemp = strtok(szTemp, "-");
			 if( chTemp != NULL )
            {
                if((chTemp1 = strrchr(chTemp,'_'))!=NULL)
                {
                    memset(szTemp2,'\0',iBytesToBeAloctd+1);
                    strcpy(szTemp2,szFileName);
                    // Copying the file recieved
                    log_name = octstr_create(szTemp2);
                }
                else
                {
                    memset(szTemp2,'\0',iBytesToBeAloctd+1);
                    strcpy(szTemp2,reversestring(szTemp,strlen(szTemp)-1));
                    counter = atoi(szTemp2);
                    //Get the length of string till - in reverse order
                    while( szTemp1[iCnt] != '-' )
                        iCnt++;

                    memset(szTemp2,'\0',iBytesToBeAloctd+1);
                    strcpy(szTemp2, reversestring(szTemp1, strlen(szTemp1)-1));
                    szTemp2[ strlen(szTemp2)- iCnt - 1 ] = '\0';
                    log_name = octstr_create(szTemp2);
                }
            }
            else
                ; //printf(" \n No - present \n");

        }// End of else checking index


        memset(szTemp,'\0',iBytesToBeAloctd+1);

        new_name = octstr_copy(log_name,0,octstr_len(log_name));
		//Increment the counter
        counter += 1;
        sprintf( szTemp, "-%d", counter);
        octstr_append_data(new_name,szTemp,strlen(szTemp));

        if ( remaining_name != NULL )
            octstr_append(new_name,remaining_name);

        new_fp = fopen(octstr_get_cstr(new_name), "a");
        if(new_fp == NULL)
        {
              error(errno, "Couldn't open logfile `%s'.", octstr_get_cstr(new_name));
              return -1;
        }
        else
            alog("alog_end");

        gwlist_lock(writers);
        //this is to prevent any other to wtite into file until name has been changed.

       if(file != NULL)
           fclose(file);

       file = new_fp;

       strcpy( gszFileName, octstr_get_cstr(new_name));

       gwlist_unlock(writers);

       alog ("alog_begins");

       info(0, "Inside access_log_file_size changed : %s",gszFileName);

       iFlag1 = 1;
		// It has incremented the counter once so no need of going in else part
        // as already gszFileName is present in RAM

    }
    else
    {
        //
        if ( iFlag1 == 0 )
        {
            strcpy( gszFileName, szFileName);
            iFlag1 = 1;
            // It has incremented the counter once so no need of going in else part
            // as already gszFileName is present in RAM
        }
    }

    gw_free(szTemp);
    gw_free(szTemp1);
    gw_free(szTemp2);
    gw_free(szTemp3);
    gw_free(szTempStr);
    gw_free(szFileName);

    octstr_destroy(new_name);
    octstr_destroy(old_name);
    octstr_destroy(original_name);
    octstr_destroy(remaining_name);
    octstr_destroy(log_name);
	
	//debug("accesslog",0,"accesslog_file_size: Ending");
		
    return 0;
}




