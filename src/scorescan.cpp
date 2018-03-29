#include <iostream>
#include <fstream>
#include <string>

#include <zbar.h>
#include <opencv2/core/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>

#include "nanosvg.h"
//#include "json.hpp"

using namespace std;
using namespace cv;
using namespace zbar;
//using namespace nlohmann;

void sortPointsCW(vector<Point>& points)
{
	std::sort(points.begin(), points.end(), 	[](cv::Point pt1, cv::Point pt2) { return (pt1.y < pt2.y);});
	std::sort(points.begin(), points.begin()+2, [](cv::Point pt1, cv::Point pt2) { return (pt1.x < pt2.x);});
	std::sort(points.begin()+2, points.end(), 	[](cv::Point pt1, cv::Point pt2) { return (pt1.x > pt2.x);});
}

vector<Point2f> rectCorners(Rect2f rect)
{
	return {
		rect.tl(),
		{ rect.x + rect.width, rect.y },
		rect.br(),
		{rect.x, rect.y + rect.height}
	};
}

Rect2f rectFromCorners(NSVGshape* shape)
{
	return {shape->bounds[0], shape->bounds[1], (shape->bounds[2] - shape->bounds[0]), (shape->bounds[3] - shape->bounds[1])};
}

bool tryFindPage(
		Mat inPage,
		Mat& outPage,
		vector<Point2f> srcQRCorners,
		Size pageSize,
		Rect2f qrBox)
{
	// Zoom out to ensure we include the full box
	double scaleFactor = 0.7;

	// 0.4 => .19
	// 0.5 => .30
	// 0.7 => .58

	Mat warped;
	Mat scalePerspective;
	{
		auto perspective = cv::getPerspectiveTransform(srcQRCorners, rectCorners(qrBox));

		auto scale = getRotationMatrix2D({pageSize.width / 2.0f, pageSize.height / 2.0f}, 0, scaleFactor);
		scale.push_back(cv::Mat::zeros(1, 3, CV_64F));
		scale.at<double>(2,2) = 1.0;

		scalePerspective = scale * perspective;

		cv::warpPerspective(inPage, warped, scalePerspective, pageSize);
	}

	//imshow( "Display window", warped ); waitKey(0);

	// Blur the inPage.
	Mat blurred;
	cv::GaussianBlur(warped, blurred, {}, 1, 1);

	//imshow( "Display window", blurred ); waitKey(0);

	// Find edges
	Mat edged;
	cv::Canny(blurred, edged, 75, 200);

	//imshow( "Display window", edged ); waitKey(0);

	// Find contours.
	vector<vector<Point>> contours{};
	cv::findContours(edged, contours, cv::RETR_LIST, cv::CHAIN_APPROX_NONE);

	// Sort contours by area
	std::sort(contours.begin(), contours.end(), [](vector<Point> a, vector<Point> b)
			{ return cv::contourArea(a) > cv::contourArea(b); });

	for (auto& contour : contours)
	{
		vector<Point> corners{};
		cv::approxPolyDP(contour, corners, 0.10 * cv::arcLength(contour, true), true);

		if (corners.size() == 4)
		{
			sortPointsCW(corners);
			vector<Point2f> srcCorners;
			cv::Mat(corners).copyTo(srcCorners);

			double area = cv::contourArea(contour) / scaleFactor;
			if (area / pageSize.area() < 0.5){
				// Failed to scan: detected square too small.
				break;
			}

			Rect2f pageBox { {}, pageSize };
			vector<Point2f> pageCorners = rectCorners(pageBox);

			auto finalPerspective = cv::getPerspectiveTransform(srcCorners, pageCorners);

			cv::warpPerspective(inPage, outPage, finalPerspective * scalePerspective, pageSize);
			return true;
		}
	}

	return false;
}

int main (int argc, char **argv)
{
	const char* windowName = "PineScan";
	namedWindow(windowName, WINDOW_NORMAL);

	vector<vector<Point>> shapes;
	vector<string> ids;
	Size pageSize;
	Rect2f qrBox;

	{
		NSVGimage* image = NULL;
		int w, h;
		const char* filename = "/home/arlo/git/scorescan-c/scorescan/src/sheet.svg";

		printf("parsing %s\n", filename);
		image = nsvgParseFromFile(filename, "px", 150.0f);
		if (image == NULL) {
			printf("Could not open SVG image.\n");
		}

		for (auto shape = image->shapes; shape != NULL; shape = shape->next) {
			string id(shape->id);

			if (id == "page")
			{
				pageSize = {static_cast<int>(shape->bounds[2]), static_cast<int>(shape->bounds[3])};
				cout << "Page size detected " << pageSize << endl;
			}
			else if (id == "qr")
			{
				qrBox = rectFromCorners(shape);
				cout << "QR detected " << shape << endl;
			}

			//cout << "Shape #" << shape->id << ": box: " << shape->bounds[0] << ", " << shape->bounds[1] << ", "<< shape->bounds[2] << ", "<< shape->bounds[3] << endl;
			vector<Point> object{};

			for (auto path = shape->paths; path != NULL; path = path->next) {
				for (auto i = 0; i < path->npts; i++) {
					object.push_back(Point2f(path->pts[i*2], path->pts[i*2 + 1]));
				}
			}
			shapes.push_back(object);
			ids.push_back(id);
		}

		w = (int)image->width;
		h = (int)image->height;
		cout << "width" << w << "height" << h << endl;

		nsvgDelete(image);
	}

    string imageName("/home/arlo/git/scorescan-c/scorescan/src/scan.jpg"); // by default

    Mat rawImage;
    rawImage = imread(imageName.c_str(), IMREAD_GRAYSCALE); // Read the file

    Mat threshold;
    cv::threshold(rawImage, threshold, 128, 255, ThresholdTypes::THRESH_BINARY);

    if(rawImage.empty())
    {
        cout <<  "Could not open or find the rawImage" << std::endl ;
        return -1;
    }

    // create a reader
    ImageScanner scanner{};

    // configure the reader
    scanner.set_config(ZBAR_QRCODE, ZBAR_CFG_ENABLE, 1);
    scanner.set_config(ZBAR_QRCODE, ZBAR_CFG_POSITION, 1);

    // wrap image data
    Image zimage(threshold.cols, threshold.rows, "Y800", threshold.data, threshold.rows * threshold.cols);

    // scan the image for barcodes
    scanner.scan(zimage);


    int qrCount = 0;
    for(Image::SymbolIterator symbol = zimage.symbol_begin(); symbol != zimage.symbol_end(); ++symbol)
    {
        if (symbol->get_type() == ZBAR_QRCODE)
        {
        	assert(symbol->get_location_size() == 4);
        	vector<Point2f> qrCorners {
				{ static_cast<float>(symbol->get_location_x(0)), static_cast<float>(symbol->get_location_y(0)) },
				{ static_cast<float>(symbol->get_location_x(3)), static_cast<float>(symbol->get_location_y(3)) },
				{ static_cast<float>(symbol->get_location_x(2)), static_cast<float>(symbol->get_location_y(2)) },
				{ static_cast<float>(symbol->get_location_x(1)), static_cast<float>(symbol->get_location_y(1)) }
        	};

        	qrCount++;

        	string data = symbol->get_data();

        	Mat warped;
        	if (tryFindPage(rawImage, warped, qrCorners, pageSize, qrBox))
        	{
        		Mat thresholded;
        		cv::threshold(warped, thresholded, 0, 255, ThresholdTypes::THRESH_BINARY_INV | ThresholdTypes::THRESH_OTSU);

        		//imshow( "Display window", thresholded ); waitKey(0);

        		assert(shapes.size() == ids.size());
        		for (size_t i = 0; i < shapes.size(); i++)
        		{
        			Mat mask = Mat::zeros(thresholded.rows, thresholded.cols, thresholded.type());
        			cv::drawContours(mask, shapes, i, 255, -1);

        			const int expand_px = 5;
        			auto kernel = Mat::ones({expand_px, expand_px}, CV_8U);

        			Mat eroded;
        			cv::dilate(mask, eroded, kernel);

        			double filled = cv::mean(thresholded, mask).val[0] / 255.0;

        			//float filled = static_cast<float>(cv::countNonZero(masked)) / static_cast<float>(cv::countNonZero(mask));

        			if (filled > 0.3)
        			{
        				cv::drawContours(warped, shapes, i, 0, 4);
        			}

        			cout << ids[i] << ": " << filled << endl;

        		}

        		//imshow(windowName, warped ); waitKey(0);
        		//break;
        	}
        }
    }

    cout << "Found " << qrCount << " QR codes" << endl;

    // clean up
    zimage.set_data(NULL, 0);

    return(0);
}
