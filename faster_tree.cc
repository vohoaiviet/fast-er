#include <tag/stdpp.h>
#include <gvars3/instances.h>
 
#include "faster_tree.h"

using namespace std;
using namespace tag;
using namespace CVD;
using namespace GVars3;



///Detect corners with nonmaximal suppression in an image. This contains a large amount of
///configurable debugging code to verify the correctness of the detector by comparing different
///implementations.
///
///@param im The image to detect corners in.
///@param detector The corner detector.
///@param threshold The detector threshold.
///@param scores This image will be used to store the corner scores for nonmaximal suppression and is
///              the same size as im. It is passed as a parameter since allocation of an image of this
///              size is a significant expense.
vector<ImageRef> tree_detect_corners(const Image<byte>& im, const tree_element* detector, int threshold, Image<int> scores)
{
	ImageRef tl, br, s;
	rpair(tl,br) = detector->bbox();
	s = im.size();

	int ymin = 1 - tl.y, ymax = s.y - 1 - br.y;
	int xmin = 1 - tl.x, xmax = s.x - 1 - br.x;

	ImageRef pos;
	scores.zero();
	
	vector<int> corners;
	
	block_bytecode f2 = detector->make_fast_detector(im.size().x);

	f2.detect(im, corners, threshold, xmin, xmax, ymin, ymax);
	

	if(GV3::get<bool>("debug.verify_detections"))
	{
		//Detect corners using slowest, but most obvious detector, since it's most likely to 
		//be correct.
		vector<ImageRef> t;
		for(pos.y = ymin; pos.y < ymax; pos.y++)
		{
			for(pos.x = xmin; pos.x < xmax; pos.x++)
				if(detector->detect_corner(im, pos, threshold))
					t.push_back(pos);
		}

		//Verify detected corners against this result
		if(t.size() == corners.size())
		{
			for(unsigned int i=0; i < corners.size(); i++)
				if(im.data() + corners[i] != & im[t[i]])
				{
					cerr << "Fatal error: standard and fast detectors do not match!\n";
					cerr << "Same number of corners, but different positions.\n";
					exit(1);
				}
		}
		else
		{
			cerr << "Fatal error: standard and fast detectors do not match!\n";
			cerr << "Different number of corners detected.\n";
			cerr << corners.size() << " " << t.size() << endl;
			exit(1);
		}
	}



	//Compute scores
	for(unsigned int j=0; j < corners.size(); j++)
	{
		int i=threshold + 1;
		while(1)
		{
			int n = f2.detect(im.data() + corners[j], i);
			if(n != 0)
				i += n;
			else
				break;
		}
		scores.data()[corners[j]] = i-1;
	}

	if(GV3::get<bool>("debug.verify_scores"))
	{
		//Compute scores using the obvious, but slow recursive implementation.
		//This can be used to test the no obvious FAST implementation and the
		//non obviouser JIT implementation, if it ever exists.
		for(unsigned int j=0; j < corners.size(); j++)
		{
			int i=threshold + 1;
			ImageRef pos =  im.pos(im.data() + corners[j]);
			while(1)
			{
				int n = detector->detect_corner(im, pos, i);
				if(n != 0)
					i += n;
				else
					break;
			}

			if(scores.data()[corners[j]] != i-1)
			{
				cerr << "Fatal error: standard and fast scores do not match!\n";
				cerr << "Different score detected at  " << pos << endl;
				exit(1);
			}
		}
	}


	//Perform non-max suppression the simple way
	vector<ImageRef> nonmax;
	int d = im.size().x;
	for(unsigned int i=0; i < corners.size(); i++)
	{
		int o = corners[i];
		int v = scores.data()[o];

		if( v > *(scores.data() + o + 1    )  &&
		    v > *(scores.data() + o - 1    )  &&
		    v > *(scores.data() + o +d + 1 )  &&
		    v > *(scores.data() + o +d     )  &&
		    v > *(scores.data() + o +d - 1 )  &&
		    v > *(scores.data() + o -d + 1 )  &&
		    v > *(scores.data() + o -d     )  &&
		    v > *(scores.data() + o -d - 1))
		{
			nonmax.push_back(ImageRef(o %d, o/d));
		}
	}

	return nonmax;
}


vector<string> split(const string& s)
{
	istringstream i(s);

	vector<string> v;

	while(!i.eof())
	{
		string s;
		i >> s;
		if(s != "")
			v.push_back(s);
	}
	return v;
}

struct ParseError{};

template<class C> C ato(const string & s)
{
	istringstream i(s);
	C c;
	i >> c;

	if(i.bad())
		throw 	ParseError();
	
	return c;
}


tree_element* load_a_tree(istream& i)
{	
	string line;
	getline(i, line);

	vector<string> tok = split(line);

	if(tok.size() == 0)
		throw ParseError();

	if(tok[0] == "Is")
	{
		if(tok.size() != 7)
			throw ParseError();

		return new tree_element(ato<bool>(tok[2]));
	}
	else
	{
		if(tok.size() != 5)
			throw ParseError();

		int offset = ato<int>(tok[0]);

		auto_ptr<tree_element> t1(load_a_tree(i));
		auto_ptr<tree_element> t2(load_a_tree(i));
		auto_ptr<tree_element> t3(load_a_tree(i));
		auto_ptr<tree_element> ret(new tree_element(t1.release(), t2.release(), t3.release(), offset));	

		return ret.release();

	}
}


