#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <vector>

#include <zbar.h>
#include <opencv2/core/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>

#include "nanosvg.h"

using namespace std;
using namespace cv;
using namespace zbar;

const char* windowName = "PineScan";
const int KEY_ESC = 27;
const int KEY_A = 97;

struct SVGShape
{
	SVGShape() = default;

	SVGShape(NSVGshape* shape)
		:
			Id{shape->id},
			BoundingBox{shape->bounds[0], shape->bounds[1], (shape->bounds[2] - shape->bounds[0]), (shape->bounds[3] - shape->bounds[1])}
	{
		for (auto path = shape->paths; path != nullptr; path = path->next) {
			for (auto i = 0; i < path->npts; i++) {
				Outline.push_back(Point2f(path->pts[i*2], path->pts[i*2 + 1]));
			}
		}
	}

	string Id;
	vector<Point> Outline;
	Rect2f BoundingBox;
};


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
	// Estimate the transformation using the location from the QR code
	auto perspectiveTransform = cv::getPerspectiveTransform(srcQRCorners, rectCorners(qrBox));

	// Zoom out to include a larger area around the page to ensure we've got the whole page.
	double scaleFactor = 0.7;
	auto scale = getRotationMatrix2D({pageSize.width / 2.0f, pageSize.height / 2.0f}, 0, scaleFactor);
	scale.push_back(cv::Mat::zeros(1, 3, CV_64F));
	scale.at<double>(2,2) = 1.0;
	Mat scalePerspective = scale * perspectiveTransform;
	Mat warped;
	cv::warpPerspective(inPage, warped, scalePerspective, pageSize);

	// Blur to remove noise.
	Mat blurred;
	cv::GaussianBlur(warped, blurred, {}, 1, 1);

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
		// Convert contours to polygon vertices.
		vector<Point> corners{};
		cv::approxPolyDP(contour, corners, 0.10 * cv::arcLength(contour, true), true);

		// Look for 4-cornered shapes
		if (corners.size() == 4)
		{
			// Order the points.
			sortPointsCW(corners);

			// Convert from Point to Point2f
			vector<Point2f> corners2f;
			cv::Mat(corners).copyTo(corners2f);

			// Check that the area is large enough to likely be our box.
			double area = cv::contourArea(contour) / scaleFactor;
			if (area / pageSize.area() < 0.5){
				// Area square too small.
				break;
			}

			Rect2f pageBox { {}, pageSize };
			vector<Point2f> pageCorners = rectCorners(pageBox);

			// Compute the transform to the corners of the rectangle
			auto finalPerspective = cv::getPerspectiveTransform(corners2f, pageCorners);
			cv::warpPerspective(inPage, outPage, finalPerspective * scalePerspective, pageSize);
			return true;
		}
	}

	// Failed to find corners.
	return false;
}

map<string, SVGShape> findSVGShapes(const string filename)
{
	map<string, SVGShape> shapes;
	NSVGimage* image = nullptr;

	image = nsvgParseFromFile(filename.c_str(), "px", 150.0f);
	if (image == nullptr) {
		return {};
	}

	for (auto shape = image->shapes; shape != nullptr; shape = shape->next) {
		shapes[shape->id] = SVGShape(shape);
	}

	nsvgDelete(image);

	return shapes;
}

vector<map<string, double>> scanImage(
		ImageScanner& scanner,
		const Mat& rawImage,
		const Size pageSize,
		const Rect2f qrBox,
		const map<string, SVGShape>& shapes) {

	vector<map<string, double>> results;

	Mat threshold;
	cv::threshold(rawImage, threshold, 128, 255, ThresholdTypes::THRESH_BINARY);

	// wrap image data
	Image zimage(rawImage.cols, rawImage.rows, "Y800", threshold.data, rawImage.rows * rawImage.cols);

	// scan the image for barcodes
	scanner.scan(zimage);

	for(Image::SymbolIterator symbol = zimage.symbol_begin(); symbol != zimage.symbol_end(); ++symbol) {
		if (symbol->get_type() == ZBAR_QRCODE) {
			assert(symbol->get_location_size() == 4); // All QR codes have 4 corners
			vector<Point2f> qrCorners {
				{ static_cast<float>(symbol->get_location_x(0)), static_cast<float>(symbol->get_location_y(0)) },
				{ static_cast<float>(symbol->get_location_x(3)), static_cast<float>(symbol->get_location_y(3)) },
				{ static_cast<float>(symbol->get_location_x(2)), static_cast<float>(symbol->get_location_y(2)) },
				{ static_cast<float>(symbol->get_location_x(1)), static_cast<float>(symbol->get_location_y(1)) }
			};

			map<string, double> bubbles;
			string data = symbol->get_data();

			Mat warped;
			if (tryFindPage(rawImage, warped, qrCorners, pageSize, qrBox))
			{
				Mat thresholded;
				cv::threshold(warped, thresholded, 0, 255, ThresholdTypes::THRESH_BINARY_INV | ThresholdTypes::THRESH_TRIANGLE);

				vector<vector<Point>> shapeVector(1);

				for (auto&& shape : shapes)
				{
					shapeVector[0] = shape.second.Outline;
					Mat mask = Mat::zeros(thresholded.rows, thresholded.cols, thresholded.type());
					cv::drawContours(mask, shapeVector, 0, 255, -1);

					const int expand_px = 5;
					auto kernel = Mat::ones({expand_px, expand_px}, CV_8U);

					Mat eroded;
					cv::dilate(mask, eroded, kernel);

					double filled = cv::mean(thresholded, eroded).val[0] / 255.0;

					if (filled > 0.5) {
						cv::drawContours(warped, shapeVector, 0, 0, 4);
					}

					bubbles[shape.first] = filled;
				}

				imshow(windowName, warped );
				int key = waitKey(0);
				if (key == KEY_A) {
					results.push_back(bubbles);
				}
			}
		}
	}

	// clean up
	zimage.set_data(NULL, 0);
	return results;
}

void printResult(map<string, double> result)
{
	bool first = true;
	cout << "{";
	for (auto&& field: result) {
		if (first) {
			first = false;
		} else {
			cout << ',';
		}
		cout << '"' << field.first << "\":" << field.second;
	}
	cout << "}" << endl;
}

int main(int argc, char **argv)
{
	if (argc < 3)
	{
		cout << "Usage: "<< argv[0] << " SvgFile [ImageFile | CameraNumber]" << endl;
		return -1;
	}

	string svgFile(argv[1]);
	string imageName(argv[2]);

	int camera(-1);
	bool liveCapture(false);

	if (imageName.size() == 1 && imageName[0] >= '0' && imageName[0] <= '9') {
		camera = imageName[0] - '0';
		liveCapture = true;
	}

	namedWindow(windowName, WINDOW_NORMAL);

	map<string, SVGShape> shapes = findSVGShapes(svgFile);

	if (shapes.count("page") == 0) {
		cout << "Error: Could not find #page in SVG" << endl;
		return -1;
	}

	if (shapes.count("qr") == 0) {
		cout << "Error: Could not find #qr in SVG" << endl;
		return -1;
	}

	// Find the QR code location in the SVG
	Rect2f qrBox = shapes["qr"].BoundingBox;

	// Find the size of the page rectangle in the SVG
	Rect2f pageBox = shapes["page"].BoundingBox;
	Size pageSize = {static_cast<int>(pageBox.x + pageBox.width), static_cast<int>(pageBox.y + pageBox.height)};

	// Configure the QR code reader
	ImageScanner scanner{};
	scanner.set_config(ZBAR_QRCODE, ZBAR_CFG_ENABLE, 1);
	scanner.set_config(ZBAR_QRCODE, ZBAR_CFG_POSITION, 1);

	Mat rawImage;
	VideoCapture cap;

	if (!liveCapture) {
		rawImage = imread(imageName.c_str(), IMREAD_GRAYSCALE);
		if (rawImage.empty()) {
			cout <<  "Could not open or find the rawImage" << std::endl ;
			return -1;
		}
	} else {
		cap = VideoCapture(camera); // open the default camera
		if(!cap.isOpened()) { // check if we succeeded
			cout << "Failed to open camera" <<endl;
			return -1;
		}

		cap.set(CV_CAP_PROP_FRAME_WIDTH,1080);
		cap.set(CV_CAP_PROP_FRAME_HEIGHT,720);
	}

	while(true) {
		if (liveCapture) {
			Mat frame;
			cap >> frame; // get a new frame from camera
			cvtColor(frame, rawImage, COLOR_BGR2GRAY);
			imshow(windowName, rawImage);
			int key = waitKey(1);

			if (key != -1) {
				// cout << "Key = " << key << endl;
			}

			if (key == KEY_ESC) { // ESC
				break;
			}
		}

		auto results = scanImage(scanner, rawImage, pageSize, qrBox, shapes);
		// 1 line per scan, JSON key-value pairs.
		for (auto&& result : results) {
			printResult(result);
		}

		if (!liveCapture) {
			break;
		}
	}

    return(0);
}
