    #include "httpd.h"
    #include "http_config.h"
    #include "http_log.h"
    #include "apr_strings.h"
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <unistd.h>
    #include <string.h>
    #include <inttypes.h>
    #include <ctype.h>
    #include <stdlib.h>
    #include <stdio.h>
    #include <regex.h>

    static size_t MAXB = 512;
    static size_t MAXPATH = 4096;  /* too bad no OS agnostic params */

/* 
 * 	strings that can be overridden by httpd.conf directives
*/
    static const char *objectStore = "/lib_content38/objectStore/";
    static const char *dataStore0 = "/lib_content38/dataStore/";
    static const char *dataStore1 = "/lib_content39/dataStore/";
    static const char *locationPreamble = "<foxml:contentLocation TYPE=\"INTERNAL_ID\" REF=\"";
    static const char *contentLocationMarker = "content+content";
    static const char *iipsrvexe = "/usr/libexec/iipsrv/iipsrv.fcgi";

/*
 *	simple conf assignments - pluck string from httpd.conf and set variables
*/
    const char *setObjectStore(cmd_parms *cmd, void *cfg, const char *arg) {
	objectStore = arg;
	return NULL;
    }


    const char *setDataStore0(cmd_parms *cmd, void *cfg, const char *arg) {
	dataStore0 = arg;
	return NULL;
    }

    const char *setDataStore1(cmd_parms *cmd, void *cfg, const char *arg) {
	dataStore1 = arg;
	return NULL;
    }


    const char *setLocationPreamble(cmd_parms *cmd, void *cfg, const char *arg) {
	locationPreamble = arg;
	return NULL;
    }


    const char *setContentLocationMarker(cmd_parms *cmd, void *cfg, const char *arg) {
	contentLocationMarker = arg;
	return NULL;
    }

    const char *setIipsrvexe(cmd_parms *cmd, void *cfg, const char *arg) {
	iipsrvexe = arg;
	return NULL;
    }

/*
 *	link the variable names to httpd.conf directive strings and limit args to 1
 *	See above, these get lower-cased by apache on fedora02, no matter how they are
 *	listed in conf file.
*/
    static const command_rec iiifDirectives[] =
    {
	AP_INIT_TAKE1("objectstore", setObjectStore, NULL, ACCESS_CONF, "path to fedora objects"),
	AP_INIT_TAKE1("datastore1", setDataStore1, NULL, ACCESS_CONF, "0 path to fedora data"),
	AP_INIT_TAKE1("datastore0", setDataStore0, NULL, ACCESS_CONF, "1 path to fedore data"),
	AP_INIT_TAKE1("locationpreamble", setLocationPreamble, NULL, ACCESS_CONF, "lead-in for stream value"),
	AP_INIT_TAKE1("contentlocationmarker", setContentLocationMarker, NULL, ACCESS_CONF, "string indicating locationline"),
	AP_INIT_TAKE1("iipsrvexe", setIipsrvexe, NULL, ACCESS_CONF, "path iipsrv cgi"),
	{ NULL }
	
    };

/*
 *==================================================================
 *	routines called by hook function iiiflinks to mangle strings 
 *		in various ways...
 *==================================================================
*/

/*      
 * use the incoming fedora id to figure out real filename on disk
 *    note: asks for space in apr_psprintf call
*/

    char *getExtURLfromRequest(request_rec *r) {

	const char *regex_text = "iiif/([^:]*):([[:digit:]]{2})?([[:digit:]]{2})?([[:digit:]]{2})?([[:digit:]]{2})?([[:digit:]]{1})?/full(.*)$";
	int max_matches = 8;
	regmatch_t mymatches[max_matches];
	int status = 0;
	int i = 0;
	char matched_strs[max_matches][128];
	regex_t myregex;
	char *badret = "";


	if (r->the_request) {
		status = regcomp(&myregex,regex_text,REG_EXTENDED|REG_NEWLINE);
		if (status == 0) { 
		    status = regexec(&myregex,r->the_request,max_matches,mymatches,0);
		    if (status == 0){ 
			for (i=0; i < max_matches;i++) {	
				int start, finish;
				start = mymatches[i].rm_so;
				finish = mymatches[i].rm_eo;
				if (start == -1) {
					sprintf(matched_strs[i],"%s","");
					continue;
				}
				sprintf(matched_strs[i],"%.*s",(finish-start),(r->the_request)+start);
			}
			return apr_psprintf(r->pool,"info:fedora/%s:%s%s%s%s%s",
				matched_strs[1],
				matched_strs[2],
				matched_strs[3],
				matched_strs[4],
				matched_strs[5],
				matched_strs[6]);
		    } 
		
		} 

	} 
	return badret;
    }

	

/*
 * url encode a URL 
*/
    void encodeURL(char *URLstr, char *encbuf) {
        static char hex[] = "0123456789ABCDEF";
        char *ebufptr;
	char *URLptr;
	ebufptr = encbuf;
	URLptr = URLstr;

        while (*URLptr) {
                if( isalnum(*URLptr) || *URLptr == '-' || *URLptr == '_' || *URLptr == '.' || *URLptr == '~' ){
                        *ebufptr++ = *URLptr;
                } else if( *URLptr == ' ' ) {
                        *ebufptr++ = '+';
                } else{
                        *ebufptr++ = '%',
                        *ebufptr++ = hex[((*URLptr) >> 4) & 15 ];
                        *ebufptr++ = hex[((*URLptr) & 15) & 15];
                }
                URLptr++;
        }
        *ebufptr = '\0';
    }


/*
 *  turn encoded url into a long using mod_adler
*/
    void adleURL(char *encURLstr, char *binStr) {
        const int MOD_ADLER = 65521;
	char buffer[MAXB];
        unsigned long adler_res;

        unsigned char dval;
        unsigned long a = 1, b = 0;
        size_t  index, j;

        for (index = 0; index < strlen(encURLstr); ++index) {
                dval = (unsigned char)(encURLstr[index]);
                a = (a + dval) % MOD_ADLER;
                b = (b + a) % MOD_ADLER;
        }

        adler_res = (b << 16) | a;
        sprintf(buffer,"%lu" ,adler_res);
	index = sizeof(buffer);
        buffer[--index] = '\0';

        do {
		buffer[--index] = (adler_res & 1) ? '1' : '0';
        } while ((adler_res >>= 1) != 0);
	j = 0;
        while (index < sizeof(buffer)) {
                buffer[j++] = buffer[index++];
        }

        buffer[j] = '\0';
	strcpy(binStr,buffer);
    }



/*
 *  chunk the binary string into pieces of two and insert slashes
*/

    void getPathStr(char *binStr, char *pathstr) {
	size_t j = 0;
        char  *pathptr;

	char buffer[MAXB];
        strcpy(buffer,binStr);
        pathptr  = pathstr;
        while (buffer[j] != '\0') {
                *pathptr++  = buffer[j++];
                if ((j % 2 ) == 0) {
                        *pathptr++ = '/';
                }
        }
/*
 *  dupe the behavior in fedora code by
 *  lopping off the last subdir for some reason
 */
	if (j > 2) {
        	if ((j - 1)  % 2 ) {
                	pathptr = pathptr - 3;
        	} else {
                	--pathptr;
        	}
	}

        *pathptr = '\0';
    }



/*
 *      get the latest content stream url by grubbing thru XML in object file
*/
    void getLastContentStr(char *objFileName, char *contentbuf) {

	FILE *cfp;
        char  *csptr1, *csptr2;
	char contentsav[MAXB];
	csptr1 = contentsav;
	

        cfp = fopen(objFileName, "r");
/*
        cfp = fopen("/var/www/html/slbtest/info%3Afedora%2Fuva-lib%3A2295799","r");
*/
	*contentsav = '\0';
        if (cfp != NULL) {
                while (fgets(contentbuf,MAXB,cfp)) {
                        if (strstr(contentbuf,contentLocationMarker)) {
                                strcpy(contentsav,contentbuf);
                        }
                }

                fclose(cfp);
		*contentbuf = '\0';
                if (strstr(contentsav,locationPreamble)) {
                        csptr1 = contentsav + strlen(locationPreamble);
                        csptr2 = strrchr(csptr1,'\"');
                        if ((csptr2) && (csptr1 < csptr2)) {
                                *csptr2 = '\0';
                                strncpy(contentbuf,csptr1,MAXB);
                        }
                } 
        }
    }

/*
 *	make the dirs you need to lead up to your link
 */
    static void makepath(const char *dir) {
        char tmp[MAXPATH];
        char *p = NULL;
        size_t len;

        snprintf(tmp, sizeof(tmp),"%s",dir);
        len = strlen(tmp);
	if (len < 2) return;
	if (tmp[0] != '/') return;
        if(tmp[len - 1] == '/')
                tmp[len - 1] = 0;
        for(p = tmp + 1; *p; p++)
                if(*p == '/') {
                        *p = 0;
                        mkdir(tmp,  S_IRWXU | S_IRWXG | S_IRUSR | S_IXUSR);
                        *p = '/';
                }
        mkdir(tmp, S_IRWXU);
    }
	


/*
 *============================================================================
 * The code Apache calls for each request - have to check handler name 
 *	that gets set by SetHandler in Directory directive in config
 *	to see if it's really for you 
 *============================================================================
 */
	
    static int iiifLinks(request_rec *r) {

/*
 *	original request will be rewritten by httpd.conf rule
 *		to iiif URL with 		
 *		a filename that iiif can handle
 *		as a query string arg 
 *	get the external PID from orginial URL in request
 *	URLencode that, and then perform Adler32 on it
 *	turn the long form alder32 into a binary string
 *	chunk that string into pieces of two digits for
 *		a binary-tree directory path
 *	construct filename for fedora object out of all this
 *	read the fedora object foxl looking for filename
 *		of latest content stream 
 *	create the file that will be in the query string
 *		as a link to that content stream file
 *		so that rewritten iiif URL will work
 */
	
	char buffer[512];
	char *extURLstr;
	char *encURLstr;
	char *binStr;
	char *pathSection;
	char *objFileName;
	char *intURLstr;
	char *contentStream;
	char *streamFileName;
	char *linkFile;
	char *p, *q;


/*
 *	Note that the apache on fedora02 seems to lower-case
 *	arguements to config directives for some reason.
 */

	if (!r->handler || strncmp(r->handler,"fcgid-script",12) ) {
			return (DECLINED);
	}

	if (!(r->filename) || strncmp(r->filename,iipsrvexe,strlen(iipsrvexe))) {
			return (DECLINED);
	}


	
        extURLstr = getExtURLfromRequest(r);

	
/* take this out when it is for real */
	p = strstr(extURLstr,"slbtest");
	if (p > 0) {
		*p++ = 'u';
		*p++ = 'v';
		*p++ = 'a';
		*p++ = '-';
		*p++ = 'l';
		*p++ = 'i';
		*p++ = 'b';
	
	}
	
	
        encURLstr = apr_pcalloc(r->pool,((strlen(extURLstr) * 3) + 1));
	encodeURL(extURLstr,encURLstr);
	
	binStr = apr_pcalloc(r->pool,MAXB);
	adleURL(encURLstr, binStr);
	
	
	pathSection = apr_pcalloc(r->pool,strlen(binStr) * 2);
	getPathStr(binStr,pathSection);

	objFileName = apr_psprintf(r->pool,"%s%s%s",objectStore,pathSection,encURLstr);


	contentStream = apr_pcalloc(r->pool,MAXB);
	getLastContentStr(objFileName,contentStream);

/*
 *	if you didn't get a content stream for some 
 *	reason, no point in going on...
 */
	if (strlen(contentStream) == 0) {
		return (DECLINED);
	}


	p = contentStream;
	while (*p != '\0') {
		if (*p == '+') *p = '/';
		p++;
	}

/*
 *	do the whole dance over with data URL
 */
	char *URLpre = "info:fedora/";
	intURLstr = apr_psprintf(r->pool,"%s%s",URLpre,contentStream);
	encodeURL(intURLstr,encURLstr);

	*binStr = '\0';
	adleURL(encURLstr, binStr);
	
	pathSection = apr_pcalloc(r->pool,strlen(binStr) * 2);
	getPathStr(binStr,pathSection);

	if (*pathSection == '0') {
		streamFileName = apr_psprintf(r->pool,"%s%s%s",dataStore0,pathSection+3,encURLstr);
	} else if (*pathSection == '1') {
		streamFileName = apr_psprintf(r->pool,"%s%s%s",dataStore1,pathSection+3,encURLstr);
	} else {
/*    
 *	something bad happened, just give up
 */
		return (DECLINED);
	}
		
	

	linkFile = "";
	if (r->args) {
		p = strstr(r->args,"IIIF=");
		if (p) {
			p +=5;
			q = strstr(p,".jp2");
			if ((q) && (p < q)) {
				q +=4;
				linkFile = apr_pstrmemdup(r->pool,p,q-p);
			} else {
				linkFile = "no jp2"; 
			}
		} else {
			linkFile = "no IIIF";
		}
	}
			
	
	strcpy(buffer,linkFile);
	char *endofpath = strrchr(buffer,'/');
	if (endofpath) *endofpath = '\0';
	makepath(buffer);
	
	
	unlink(linkFile);
	symlink(streamFileName,linkFile);
	
	
	return OK;
    }
	
/*
 *============================================================================================
 *	Apache module plumbing and glue
 *===========================================================================================
 */


/*
 *	connect your function to call back from request handling
 *		use fixups hook 
 */
	
    static void iiifRegisterHooks(apr_pool_t *p) {
	ap_hook_fixups(iiifLinks, NULL, NULL, APR_HOOK_FIRST);
    }


/*
 *	general plumbing to say where you define hooks and config
 *		name of module below matches LoadModule directive 
 *		in conf file
 *		second-to-last string matches function name in this
 *			program for connecting config names to 
 *			function calls for setting the values of
 *			variables 
 *		last string matches function name for connecting
 *			name of an apache callback hook to a 
 *			function name in this program that gets 
 *			the callback
 */
    module AP_MODULE_DECLARE_DATA iiifLinks_module = {
        STANDARD20_MODULE_STUFF,
        NULL, /* create per-directory config structures */
        NULL, /* merge per-directory config structures */
        NULL, /* create per-server config structures */
        NULL, /* merge per-server config structures */
        iiifDirectives, /* command handlers */
        iiifRegisterHooks /* register hooks */
    };


