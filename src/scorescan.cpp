#include <iostream>
#include <zbar.h>
#include <opencv2/core/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <iostream>
#include <string>

using namespace std;
using namespace cv;
using namespace zbar;

RNG rng(12345);

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

	// Blur the inPage.
	Mat blurred;
	cv::GaussianBlur(warped, blurred, Size {3,3}, 0);

	// Find edges
	Mat edged;
	cv::Canny(blurred, edged, 75, 200);

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
    string imageName("/home/arlo/git/scorescan-c/scorescan/src/scan.jpg"); // by default
    if( argc > 1)
    {
        imageName = argv[1];
    }

    Mat rawImage;
    rawImage = imread(imageName.c_str(), IMREAD_GRAYSCALE); // Read the file

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
    Image zimage(rawImage.cols, rawImage.rows, "Y800", rawImage.data, rawImage.rows * rawImage.cols);

    // scan the image for barcodes
    scanner.scan(zimage);
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

        	string data = symbol->get_data();

        	Size pageSize {359, 480};
        	Point2f qrLocation {5, 5};
        	Rect2f qrBox {5, 5, 75, 75};

        	Mat warped;
        	if (tryFindPage(rawImage, warped, qrCorners, pageSize, qrBox))
        	{
        		imshow( "Display window", warped );
        		waitKey(0);
        	}
        }
    }

    // clean up
    zimage.set_data(NULL, 0);

    return(0);
}
