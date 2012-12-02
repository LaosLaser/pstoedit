/*
    drvlaos.cpp : This file is part of pstoedit
    Configurable backend for laos code format for Laser cutters.

    Contributed / Copyright 2010-2012 by: Peter Brier & Jaap Vermaas
    Developed as part of the "LAOS" open source laser driver project

    Based on drvgcode.c, Contributed / Copyright 2008 by: Lawrence Glaister VE7IT 

    Copyright (C) 1993 - 2009 Wolfgang Glunz, wglunz35_AT_pstoedit.net
    (for the skeleton and the rest of pstoedit)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

    Output documents are stuctured in the following way:

    1)Prolog
    2)Body
    3)Trailer

    The content is defined by PostScript Features. They can be read from
    the configuration file (laoscfg.ps) or from the PostScript input file
    itself. The most important features are:
    - Scale     --> translates pt to micrometers
    - Prolog    --> added to beginning of output file
    - Trailer	--> added to end of output file
    - LaserCutting Power/Speed/Freq --> value definitions while cutting
    - LaserMarking Power/Speed/Freq --> value definitions while marking
    - LaserEngraving Power/Speed/PPI --> value definitions while engraving

*/

#include "drvlaos.h"
#include I_stdio
#include I_string_h
#include I_iostream
#include I_fstream
#include I_stdlib
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <time.h>
#include <version.h>
#include "cppcomp.h"
#define PNG_DEBUG 3
#include <png.h>

//
// Constructor: load config file
// Select the right config, based on the --configname option
//
drvLAOS::derivedConstructor(drvLAOS): constructBase
{
    // driver specific initializations

    cout << "\ndrvlaos: (c) 2010-2012 Peter Brier & Jaap Vermaas (www.laoslaser.org)\n";
    if (Verbose())
    {
        cout << "drvlaos: compiled: " __DATE__ " " __TIME__ "\n";
        cout << "drvlaos: Input file: " << nameOfInputFile_p << endl;
    }  

    // make sure all features have a default value
    psfeatures["*PageLength"] = "595.280029296875"; // unit = pt = .35277 mm = 72 points per inch
    psfeatures["*PageWidth"] = "841.890014648438"; 
    psfeatures["*LaserCuttingSpeed"] = "v10";
    psfeatures["*LaserCuttingPower"] = "100%";
    psfeatures["*LaserCuttingFrequency"] = "f1000";
    psfeatures["*LaserMarkingSpeed"] = "mv100";
    psfeatures["*LaserMarkingPower"] = "m70%";
    psfeatures["*LaserMarkingFrequency"] = "mf2000";
    psfeatures["*LaserEngravingSpeed"] = "100";
    psfeatures["*LaserEngravingPower"] = "50";
    psfeatures["*LaserEngravingPPI"] = "600";
    psfeatures["*LaserEngravingMode"] = "BW";
    psfeatures["*LaserEngravingInvert"] = "False";
    psfeatures["*LaserEngravingBits"] = "1";
    psfeatures["*LaserBoundaryBox"] = "True";
    psfeatures["*Scale"] = "352.777777778";
    psfeatures["*Digits"] = "0";
    psfeatures["*Threshold"] = "0";
    psfeatures["*Prolog"] = "; Generated by pstoedit _version_ from _filename_ at _date_ \n";
    psfeatures["*Trailer"] = "; www.laoslaser.org";
    // now read features from PostScript file
    ReadFeatures(nameOfInputFile_p);
    // store global configuration values in numeric variables
    scale = atof(psfeatures["*Scale"].c_str());
    digits = atof(psfeatures["*Digits"].c_str());
    bits = atoi(psfeatures["*LaserEngravingBits"].c_str());
    bpp = pow(2, bits);
    threshold = atoi(psfeatures["*Threshold"].c_str());
    imgFactor = atof(psfeatures["*LaserEngravingPPI"].c_str()) / 72.0;

    // init some global runtime variables
    filter = _undefined; // current filter
    doMove = false; // Current position

    // define temporary filenames
    tc_outname =  full_qualified_tempnam("pscut");
    tc_out.open(tc_outname.value());
    tm_outname = full_qualified_tempnam("psmark");
    tm_out.open(tm_outname.value());
    te_outname = full_qualified_tempnam("psengrave");
    te_out.open(te_outname.value());

    pngname = full_qualified_tempnam("pngtmp");
    if (Verbose()) {
        cout << "temporary PNG written to " << pngname << endl;
        cout << "pstoedit data dir : " << drvbase::pstoeditDataDir() << endl;
    }
    globaloptions_p.pngimage.copyvalue_simple(pngname.value());
    
    char resolution[] = "-r1200x1200";
    sprintf(resolution, "-r%sx%s", psfeatures["*LaserEngravingPPI"].c_str(), psfeatures["*LaserEngravingPPI"].c_str());
    globaloptions_p.psArgs.copyvalue_simple(resolution);  /* set resolution */

#ifdef __APPLE__ 
    RSString test(drvbase::pstoeditDataDir());
#endif
#ifndef __APPLE__
    RSString test("/usr/local/share/pstoedit"):
#endif
    test += directoryDelimiter;
    test += LAOS_CONFIG_FILE;

    if (fileExists(test.value())) {
        if (Verbose()) {
            errf <<"adding include for " << test.value() << endl;
        }
        globaloptions_p.nameOfIncludeFile.copyvalue_simple(test.value());
    } else {
        errf <<"include file " << test.value() << " not found "<< endl;
    } 
}

void drvLAOS::catFile(RSString *name)
{   
    // paste temp output file to real output
    // when finished, delete the file
    string line;
    ifstream myfile (name->value());
    if (myfile.is_open()) 
    {
        myfile.seekg(0, ios::beg);
        while (myfile.good())
        {
            getline (myfile, line);
            if (line.size() > 0) 
                outf << line << endl;
        }
        myfile.close();
    }
    remove (name->value());
}
 
drvLAOS::~drvLAOS()
{
    // turn all images into one engraving layer
    engrave_images();
	// now it's time to merge the temp output files
    // into one "real" output:
    te_out.close(); catFile(&te_outname);
    tm_out.close(); catFile(&tm_outname);
    tc_out.close(); catFile(&tc_outname);
    outf << psfeatures["*Trailer"];
}

/**
*** Read the PS file features
*** Any "%%%LaosInclude: " strings are outputted in the file
**/
void drvLAOS::ReadFeatures(const char * filename)
{
    // Read all the settings in the PS file, 
    // The format in the file is: "%%BeginFeature: *LaserCuttingPower None"
    // We store  "*LaserCuttingPower" as key and "None" as value.
    ifstream psfile (filename);
    if (psfile.is_open())
    {
        string line;

        while ( psfile.good() )
        {
            getline (psfile,line);
            if ( line.find("%%%LaosInclude:" ) != string::npos)
            {
                Substitute(line, "\\n", "\n");
                outf << line.substr(15); 
            }
            if ( line.find("%%BeginFeature:" ) != string::npos)
            {
                string key="", value="";
                const char *c = line.c_str();
                int idx=0;
                while ( *c )
                {
                    if ( *c == ' ' && idx < 2) 
                    {
                        idx++;
                    }
                    else
                    {
                        if (idx == 1 ) key += *c;
                        if (idx == 2 ) value += *c;					
                    }
                    c++;
                }
                if ( key != "" )
                {
                    if (Verbose()) 
                        cout << "key: '" << key << "' value: '" << value << "'\n";
                    psfeatures[key] = value;
                }
                if ( line.find("*CustomPageSize") != string::npos)
                {
                    getline(psfile,line);
                    psfeatures["*PageLength"] = line;
                    getline(psfile,line);
                    psfeatures["*PageWidth"] = line;
                }
            }
            if (line.find("%%EndSetup") != string::npos)
                break;
        }
        psfile.close();
    }
}

/// Helper functions: Replace substring in string
int drvLAOS::Substitute(string &src, string key, string value)
{
    size_t pos=src.find(key);
    if ( pos == string::npos ) return string::npos;
    src.replace(pos, key.size(), value);  
    return pos;  
}

// Replace key with numeric value (defined scale and digits)
int drvLAOS::Substitute(string &src, string key, double value, double scale, int digits)
{
    stringstream ss;
    ss << setiosflags(ios::fixed) << setprecision(digits) << scale*value;
    return Substitute(src, key, ss.str()); 
}

// Replace key with numeric value, use global scale and offset, and specific scale and 
// offset (if defined)
int drvLAOS::Substitute(string &src, string key, double value)
{
    return Substitute(src, key, value,  scale, digits); 
}

int drvLAOS::Substitute(string &src, string key, int value)
{
    return Substitute(src, key, (double)value, 1.0, 0); 
}

int drvLAOS::Substitute( string &src, Point p )
{
    Substitute(src, "_x_", p.x_);
    Substitute(src, "_y_", p.y_);
    return 0;
}

// Move to position (directly emitted)
void drvLAOS::DoMoveTo(Point p)
{
    string s = "0 _x_ _y_\n";
    Substitute(s, p);
    if (filter == 1) tc_out << s;
    if (filter == 2) tm_out << s;
    if (filter > 2) te_out << s;
    curPos.x_ = p.x_;
    curPos.y_ = p.y_;
    doMove = false;
}

// Move to position (not directly emitted, only change current position)
void drvLAOS::MoveTo(Point p)
{
    curPos.x_ = p.x_;
    curPos.y_ = p.y_;
    doMove = true;
}

// Line from current position to new position
void drvLAOS::LineTo(Point p)
{
    if ( doMove )
    {
        string s = "0 _x_ _y_\n";
        Substitute(s, curPos);
        if (filter == 1) tc_out << s;
        if (filter == 2) tm_out << s;
        if (filter > 2) te_out << s;
        doMove = false;
    }
    string str = "1 _x_ _y_\n";
    Substitute(str, p);
    if (filter == 1) tc_out << str;
    if (filter == 2) tm_out << str;
    if (filter > 2) te_out << str;
}

// Called when a new page is started, we output the prolog and the features
void drvLAOS::open_page()
{
    //date and time of conversion
    time_t rawtime;
    struct tm *timeinfo;
    char sdate[80];
    time (&rawtime);
    timeinfo = localtime ( &rawtime );
    strftime (sdate, 80, "%c", timeinfo);
    
    // write Prolog to file:
    string p = psfeatures["*Prolog"].c_str();
    Substitute(p, "_version_", version);
    Substitute(p, "_filename_", inFileName.value());
    Substitute(p, "_date_", sdate);
    outf <<  p; 

    // add page bounding box in moves
    if (psfeatures["*LaserBoundaryBox"].compare("True") == 0)
    {
        // TODO: what happens with LandScape / Portrait???
        // TODO: is this Inkscape specific or not?
        outf << "7 201 0" << endl;
        string p = "7 202 _x_";
        Substitute(p, "_x_", atof(psfeatures["*PageLength"].c_str()));
        outf << p << endl;
        outf << "7 203 0" << endl;
        p = "7 204 _y_";
        Substitute(p, "_y_", atof(psfeatures["*PageWidth"].c_str()));
        outf << p << endl;
    }

    filterPresets(); // write filter presets to temp outfiles
}

void drvLAOS::close_page()
{
    // everything is done in drvLAOS::~drvLAOS();
}

inline Point pob(float t, const Point & p1, const Point & p2, const Point & p3, const Point & p4)
{
    double x1=p1.x_, y1=p1.y_;
    double cx1=p2.x_, cy1=p2.y_;
    double cx2=p3.x_, cy2=p3.y_;
    double x2=p4.x_, y2=p4.y_;
    // Coefficients of the parametric representation of the cubic
    double ax=cx1-x1, ay=cy1-y1;
    double bx=cx2-cx1-ax, by=cy2-cy1-ay;
    double cx=x2-cx2-ax-bx-bx;
    double cy=y2-cy2-ay-by-by;
    double x=x1+(t*((3*ax)+(t*((3*bx)+(t*cx)))));
    double y=y1+(t*((3*ay)+(t*((3*by)+(t*cy)))));
    return Point(x,y);
}

void drvLAOS::filterPresets()
{
    // tc_out << "; cutting" << endl;
    string s_speed = psfeatures["*LaserCuttingSpeed"];
    s_speed = s_speed.substr(1, (s_speed.size() -1));
    int speed = atoi(s_speed.c_str()) * 1000;
    tc_out << "7 100 " << speed << endl;
    string s_power = psfeatures["*LaserCuttingPower"];
    s_power = s_power.substr(0, s_power.size() -1);
    int power = atoi(s_power.c_str()) * 100;
    tc_out << "7 101 " << power << endl;
    // string s_freq = psfeatures["*LaserCuttingFrequency"];
    // s_freq = s_freq.substr(1, s_freq.size() -1);
    // int freq = atoi(s_freq.c_str());
    // tc_out << "7 102 " << freq << endl;

    // tm_out << "; marking" << endl;
    s_speed = psfeatures["*LaserMarkingSpeed"];
    s_speed = s_speed.substr(2, (s_speed.size() -2));
    speed = atoi(s_speed.c_str()) * 1000;
    tm_out << "7 100 " << speed << endl;
    s_power = psfeatures["*LaserMarkingPower"];
    s_power = s_power.substr(1, s_power.size() -2);
    power = atoi(s_power.c_str()) * 100;
    tm_out << "7 101 " << power << endl;
    // s_freq = psfeatures["*LaserMarkingFrequency"];
    // s_freq = s_freq.substr(2, s_freq.size() -2);
    // freq = atoi(s_freq.c_str());
    // tm_out << "7 102 " << freq << endl;

    //te_out << "; engraving (" << filter << ")" << endl;
    s_speed = psfeatures["*LaserEngravingSpeed"];
    speed = atoi(s_speed.c_str()) * 1000;
    te_out << "7 100 " << speed << endl;
    s_power = psfeatures["*LaserEngravingPower"];
    power = atoi(s_power.c_str()) * 100;
    te_out << "7 101 " << power << endl;
}

/**
*** Show a line path
**/
void drvLAOS::show_path()
{
	Point currentPoint(0.0f, 0.0f);	
    
    Filter currentFilter(currentR(), currentG(), currentB(), currentLineWidth(), currentShowType() );
    if (filter != currentFilter.operation())
        filter = currentFilter.operation();
   	Point firstPoint = pathElement(0).getPoint(0);
    // cout << "Path begins at (" << firstPoint.x_ << ", " << firstPoint.y_ << ")" << endl;
    if ((filter == 1) || (filter == 2)) 
    { 
    
	    for (unsigned int n = 0; n < numberOfElementsInPath(); n++) {
		    const basedrawingelement & elem = pathElement(n);
		
		    switch (elem.getType()) 
		    {
		        case moveto:
                {
				    const Point & p = elem.getPoint(0);
                    // cout << "MoveTo (" << p.x_ << ", " << p.y_ << ")" << endl;
				    MoveTo(p);
				    currentPoint = p;
                    		    firstPoint = p;
			        break;
                }
		        case lineto:
                {
				    const Point & p = elem.getPoint(0);
                    // cout << "LineTo (" << p.x_ << ", " << p.y_ << ")" << endl;
			        LineTo(p);
				    currentPoint = p;
			        break;
                }
		        case closepath:
                {
                    // cout << "LineTo (" << firstPoint.x_ << ", " << firstPoint.y_ << ")" << endl;
				    LineTo(firstPoint);
			        break;
                }
		        case curveto: 
                {
                    // cout << "IN CurveTo (!)" << endl;
				    const Point & cp1 = elem.getPoint(0);
				    const Point & cp2 = elem.getPoint(1);
				    const Point & ep  = elem.getPoint(2);
				    // printf("(%f,%f)-(%f,%f)-(%f,%f)-(%f,%f)\n", currentPoint.x_, currentPoint.y_, cp1.x_, cp1.y_, cp2.x_, cp2.y_, ep.x_, ep.y_);
				    // curve is approximated with a variable number or linear segments.
				    // fitpoints should be somewhere between 5 and 50 for reasonable page size plots
				    // we compute distance between current point and endpoint and use that to help
				    // pick the number of segments to use.
				    const float dist = (float) pythagoras((float)(ep.x_ - currentPoint.x_),(float)(ep.y_ - currentPoint.y_)); 
				    unsigned int fitpoints = (unsigned int)((dist / 10.0) * (scale / 10));
				    if ( fitpoints < 20 ) fitpoints = 20;
				    if ( fitpoints > 100 ) fitpoints = 100;
				
                    // cout << "MoveTo (" << currentPoint.x_ << ", " << currentPoint.y_ << ")" << endl;
				    MoveTo(currentPoint);
				    for (unsigned int s = 1; s < fitpoints; s++) 
                    {
					    const float t = 1.0f * s / (fitpoints - 1);
					    const Point pt = pob(t, currentPoint, cp1, cp2, ep);
                        // cout << "LineTo (" << pt.x_ << ", " << pt.y_ << ")" << endl;
					    LineTo(pt);
				    } 
				    currentPoint = ep;
			        break;
                }
		        default:
                {
			        errf << "\t\tFatal: unexpected case in drvlaos " << endl;
			        exit(1);
			        break;
                }
		    }
        } // for
    } 
    else
    { // if !filter=1/2
        // cout << "Unexpected filter value" << endl;
    } // else if filter != 1 /2
}

int drvLAOS::pixelValue(png_byte* ptr) {
    float val = 3.0 * MaxRGB - ptr[0] - ptr[1] - ptr[2];
    val = val / (3.0 * MaxRGB) * bpp;
    return (int) val;
}

void drvLAOS::engraveLine(int x_start, int x_end, int y) {
    png_byte* row = row_pointers[y];
    if (engravedir != 1) {
        // swap begin and end
        int x_tmp = x_start;
        x_start = x_end;
        x_end = x_tmp;
    }
    Point p (x_start * imgfactor_x, (height-y) * imgfactor_y);
    DoMoveTo(p);
        
    // create engraving data line
    unsigned int val = 0;
    int c = 0;
    int columns = abs(x_start-x_end);
    te_out << "9 " << bits << ' '<< columns;
    int x;
    // printf("engraveLine from %d to %d step %d\n", x_start, x_end, engravedir);
    if (engravedir == 1)
        for (x=x_start; x<x_end+1; x++) {
            val = val + (pixelValue(&(row[x*3])) << c);
            c += bits;
            if (c == 32) {
                te_out << ' ' << val;
                val = 0; c = 0;
            }
        }
    else 
        for (x=x_start; x>x_end-1; x--) {
            val = val + (pixelValue(&(row[x*3])) << c);
            c += bits;
            if (c == 32) {
                te_out << ' ' << val;
                val = 0; c = 0;
            }
        }
    if (c != 0 ) te_out << ' ' << val;
    te_out << endl;
         
    // line to end of bitmap data
    p.x_ = x_end * imgfactor_x;
    LineTo(p);
    engravedir = -1 * engravedir;
} 

void drvLAOS::engrave_images()
{
    int x, y;

    png_byte color_type;
    png_byte bit_depth;

    png_structp png_ptr;
    png_infop info_ptr;
    int number_of_passes;
    // png_bytep * row_pointers;
    engravedir = 1;

    if (psfeatures["*LaserEngravingMode"].compare("BW") == 0)
    {
            if (! fileExists(pngname.value()))
            {
                errf << "PNG image " << pngname.value() << " not found, skip engraving" << endl;
                return;
            }
    
            png_byte header[8];    // 8 is the maximum size that can be checked

            /* open file and test for it being a png */
            FILE *fp = fopen(pngname.value(), "rb");
            if (!fp) {
                errf << "[read_png_file] File " << pngname.value() << "could not be opened for reading" << endl;
                return;
            }
            fread(header, 1, 8, fp);
            if (png_sig_cmp(header, 0, 8)) {
                errf << "[read_png_file] File " << pngname.value() << "is not recognized as a PNG file" << endl;
                return;
            }
            filter = _stroke_engrave;

            /* initialize stuff */
            png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

            if (!png_ptr) {
                errf << "[read_png_file] png_create_read_struct failed" << endl;
                return;
            }

            info_ptr = png_create_info_struct(png_ptr);
            if (!info_ptr) {
                errf << "[read_png_file] png_create_info_struct failed" << endl;
                return;
            }

            if (setjmp(png_jmpbuf(png_ptr))) {
                errf << "[read_png_file] Error during init_io" << endl;
                return;
            }

            png_init_io(png_ptr, fp);
            png_set_sig_bytes(png_ptr, 8);

            png_read_info(png_ptr, info_ptr);

            width = png_get_image_width(png_ptr, info_ptr);
            height = png_get_image_height(png_ptr, info_ptr);
            color_type = png_get_color_type(png_ptr, info_ptr);
            bit_depth = png_get_bit_depth(png_ptr, info_ptr);

            number_of_passes = png_set_interlace_handling(png_ptr);
            png_read_update_info(png_ptr, info_ptr);

            /* read file */
            if (setjmp(png_jmpbuf(png_ptr))) {
                errf << "[read_png_file] Error during read_image" << endl;
                return;
            }
            row_pointers = (png_bytep*) malloc(sizeof(png_bytep) * height);
            for (y=0; y<height; y++)
                row_pointers[y] = (png_byte*) malloc(png_get_rowbytes(png_ptr,info_ptr));

            png_read_image(png_ptr, row_pointers);

            fclose(fp);

            imgfactor_y = atof(psfeatures["*PageWidth"].c_str()) / height;
            // Factor for Y-Axis should match X-Axis: does this work for LandScape/Portrait?
            imgfactor_x = atof(psfeatures["*PageLength"].c_str()) / width;
            if ((imgfactor_x - imgfactor_y) > 0.001) {
                errf << "Image Scaling error: X and Y scaling is not the same: X=" << imgfactor_x << ", Y=" << imgfactor_y << endl;
            }
            if (Verbose()) 
                cout << "Image factor X=" << imgfactor_x << ", Y=" << imgfactor_y << endl;
            
            if (png_get_color_type(png_ptr, info_ptr) != PNG_COLOR_TYPE_RGB) {
                cout << "[process_file] input file is not PNG_COLOR_TYPE_RGB, cannot handle this type" << endl;
                return;
            }

            for (y=0; y<height; y++) {
                png_byte* row = row_pointers[y];
                for (x=0; x<width; x++) {
                    png_byte* ptr = &(row[x*3]);
                    if (ptr[0]+ptr[1]+ptr[2] != 3*MaxRGB) break;
                }
                int first = x;
                for (x=width-1; x>0; --x) {
                    png_byte* ptr = &(row[x*3]);
                    if (ptr[0]+ptr[1]+ptr[2] != 3*MaxRGB) break;
                }
                int last = x;

                if (first<last) {
                    engraveLine(first, last, y);
                    /*
                    printf("Line %d starts at pixel %d\n", y, first);
                    printf("Line %d ends at pixel %d\n", y, last);
                    for (x=first; x<last+1; x++) {
                        png_byte* ptr = &(row[x*3]);
                        printf("Pixel at position [ %d - %d ] has RGB values: %d - %d - %d \n",
                            x, y, ptr[0], ptr[1], ptr[2]);
                    }
                    */
                }
            }   
            remove (pngname.value());
    }
}

static DriverDescriptionT < drvLAOS > D_laos
(   "laos", "configurable laos format", 
    "See also:  \\URL{http://wwwlaoslaser.org/} ","lgc", true,	// if backend supports subpathes
    // if subpathes are supported, the backend must deal with
    // sequences of the following form
    // moveto (start of subpath)
    // lineto (a line segment)
    // lineto 
    // moveto (start of a new subpath)
    // lineto (a line segment)
    // lineto 
    //
    // If this argument is set to false each subpath is drawn 
    // individually which might not necessarily represent
    // the original drawing.
    true,	// if backend supports curves
    false,   // if backend supports elements with fill and edges
    false,	// if backend supports text
    DriverDescription::noimage,	// no image support
    DriverDescription::normalopen, false,	// if format supports multiple pages in one file
    false /*clipping */ 
);
