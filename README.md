# iiifLinks
an Apache mod that will intercept requests for images bound for iiif server and make a soft link from the data content stream for the requested fedora object to the file that Apache is about to put into the rewritten URL as the query string arg for that request

Here's the basic flow:

A user requests http://fedora02.lib.virginia.edu/iiif/uva-lib:2295196/full/!1200,1500/0/default.jpg

Apache's mod_rewrite maps it to http://fedora02.lib.virginia.edu/iipsrv?IIIF=/var/www/html/uva-lib/22/95/19/6/2295196.jp2/full/!1200,1500/0/default.jpg, using the following rewrite rule:

RewriteRule ^/iiif/uva-lib[0-9][0-9])([0-9][0-9])([0-9][0-9])([0-9])/(.*)$ /iipsrv?IIIF=/var/www/html/uva-lib/$1/$2/$3/$4/$1$2$3$4.jp2/$5 [PT]

Then the IIP server gets the file at the path /var/www/html/uva-lib/22/95/19/6/2295196.jp2 and returns the appropriate portion/scale.

This only works because we've already made that file as a symbolic link to the current JP2 file for the given pid.

/var/www/html/uva-lib/22/95/19/6/2295196.jp2 -> /lib_content39/dataStore/10/11/01/10/10/01/11/00/01/00/01/10/01/11/info%3Afedora%2Fuva-lib%3A2295196%2Fcontent%2Fcontent.0

What we need to happen is, instead of having a rewrite rule using mod_rewrite, we have a new module that rewrites the URL to compute the path that's currently pre-cached in the symlink. The logic that generated the symlink is linked from previous comments and would be the heart of the apache module. It basically boils down to:
1. find the XML file for the fedora object with the given pid (uva-lib:2295196).
2. parse the XML and find the last version of the "content" datastream. (if there is none, we should return a 404)
3. rewrite the request to reference the actual path of the image...


BUT, turns out we can't pass back the actual file name as argument because the iipsrv code does not handle filenames URLencoded special characters.
SO, we will revert to original rewrite rule and make this mod work out the acutal file name as in steps 1-3 above, 
but then create one soft link on the fly to the file the rewrite rule is about to pass back.  
This saves us from having to make a giant tree of links ot every stream, requested or not, 
and eliminates any lag between ingesting the content and running a batch job to make soft links.

For general instructions on writing Apache modules, see https://httpd.apache.org/docs/2.4/developer/modguide.html


