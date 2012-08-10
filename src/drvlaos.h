#ifndef __drvLAOS_H
#define __drvLAOS_H

/* 
	drvLAOS.h : This file is part of pstoedit
    Configurable backend for laos code format for Laser cutters.
    
    Contributed / Copyright 2010-2012 by: Peter Brier & Jaap Vermaas
    Developed as part of the "LAOS" open source laser driver project

    Based on drvgcode.c, Contributed / Copyright 2008 by: Lawrence Glaister VE7IT

	Copyright (C) 1993 - 2009 Wolfgang Glunz, wglunz@pstoedit.net

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
*/

#include "drvbase.h"
#ifdef _WIN32
// Magic++ needs WIN32
#ifndef WIN32
#define WIN32
#endif
#endif
#include <Magick++.h>
using namespace std;
using namespace Magick;

#include <float.h>
#include <map>

#ifdef __APPLE__
#define LAOS_CONFIG_FILE "/usr/local/share/pstoedit/laoscfg.ps"
#endif
#ifndef __APPLE__
#define LAOS_CONFIG_FILE "laoscfg.ps"
#endif

class drvLAOS : public drvbase {

public:

	derivedConstructor(drvLAOS);
	~drvLAOS(); // Destructor


	class DriverOptions : public ProgramOptions {
	public:
		OptionT < RSString, RSStringValueExtractor> configfile;
		DriverOptions():
		configfile(true,"-configfile", "string", 0, "specify configuration file (default " LAOS_CONFIG_FILE ")", 0, (const char*)LAOS_CONFIG_FILE)
		{
			ADD( configfile );
		}
	}*options;

#include "drvfuncs.h"
	
public:
	
private:
    enum FilterType { _undefined, _cut, _mark, _stroke_engrave, _fill_engrave };
    class Filter 
    {
	    public: 
            Filter() { r = g = b = w = -1.0; t=-1; }
            Filter(double r, double g, double b, double w, int t) 
            {
                this->r  = r;
                this->g  = g;
                this->b  = b;
                this->w  = w;
                this->t = t;
            }
	        double r,g,b,w;
            int t;
		    bool InRange(double r, double g, double b, double w, int t) 
		    {
		        int result = true;
			    if ( this->r >= 0.0 ) if ( r != this->r ) result = false;
			    if ( this->g >= 0.0 ) if ( g != this->g ) result = false;
			    if ( this->b >= 0.0 ) if ( b != this->b ) result = false;
			    if ( this->w >= 0.0 ) if ( w != this->w ) result = false;
			    if ( this->t >= 0.0 ) if ( t != this->t ) result = false;
			    return result;
		    }
            bool operator==(const Filter & f2) const { 
   		        int result = true;
                if ( r >= 0.0 ) if ( r != f2.r ) result = false;
		        if ( g >= 0.0 ) if ( g != f2.g ) result = false;
			    if ( b >= 0.0 ) if ( b != f2.b ) result = false;
			    if ( w >= 0.0 ) if ( w != f2.w ) result = false;
			    if ( t >= 0 ) if ( t != f2.t ) result = false;
			    return result;
            }
            FilterType operation() {
                if ((r==1.0) && (g==0.0) && (b == 0.0) && (w <= 1.0) && (t == 0))
                    return _cut;
                if ((r==0.0) && (g==0.0) && (b == 0.0) && (w <= 1.0) && (t == 0))
                    return _mark;
                if (t == 0)
                    return _stroke_engrave;
                else
                    return _fill_engrave;
            }
	};
    class EngraveImage
    {
        public:
            string filename;
            Point p1;
            Point p2;
            EngraveImage(string filename, Point p1, Point p2)
            {
                this->filename = filename;
                this->p1.x_ = p1.x_; this->p1.y_ = p1.y_;
                this->p2.x_ = p2.x_; this->p2.y_ = p2.y_;
            }
    };
    ofstream tc_out, tm_out, te_out;
    RSString tc_outname, tm_outname, te_outname, pngname;
    map<string,string> psfeatures;  
    list<EngraveImage> engraveImg;
	FilterType filter;
	float scale, imgFactor;
	int digits, bits, threshold, bpp;
	Point curPos, imgOffset; // Current position
	bool doMove;
    Image *imageptr;
	int Substitute(string &src, string key, double value, double scale, int digits);
	int Substitute(string &src, string key, int value);
	int Substitute(string &src, string key, string value);	  
	int Substitute(string &src, string key, double value);
	int Substitute( string &src, Point p );
	void LineTo(Point p);
    void ImageLineTo(Point p, list<Coordinate> *pcl);
	void DoMoveTo(Point p);
	void MoveTo(Point p);
	void ReadFeatures(const char * filename);
    void filterPresets();
    void catFile(RSString *name);
    int pixelValue(PixelPacket *pix);
    void engrave_images();
};
#endif



