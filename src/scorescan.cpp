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
using namespace zbar;

const char* windowName = "PineScan";

const int KEY_ESC = 27;
const int KEY_Q = 113;
const int KEY_A = 97;
const int KEY_SPACE = 32;
const int KEY_RIGHT = 83;
const int KEY_LEFT = 81;
const int KEY_DOWN = 84;

struct SVGShape {
	SVGShape() = default;

	SVGShape(NSVGshape* shape) :
		Id { shape->id },
		BoundingBox {
			shape->bounds[0],
			shape->bounds[1],
			shape->bounds[2] - shape->bounds[0],
			shape->bounds[3] - shape->bounds[1] }
	{
		for (auto path = shape->paths; path != nullptr; path = path->next) {
			for (auto i = 0; i < path->npts; i++) {
				Outline.push_back(
						cv::Point2f(path->pts[i * 2], path->pts[i * 2 + 1]));
			}
		}
	}

	string Id;
	vector<cv::Point> Outline;
	cv::Rect2f BoundingBox;
};

void sortPointsCW(vector<cv::Point>& points) {
	std::sort(points.begin(), points.end(),
			[](cv::Point pt1, cv::Point pt2) {return (pt1.y < pt2.y);});
	std::sort(points.begin(), points.begin() + 2,
			[](cv::Point pt1, cv::Point pt2) {return (pt1.x < pt2.x);});
	std::sort(points.begin() + 2, points.end(),
			[](cv::Point pt1, cv::Point pt2) {return (pt1.x > pt2.x);});
}

vector<cv::Point2f> rectCorners(cv::Rect2f rect) {
	return {
		rect.tl(),
		{rect.x + rect.width, rect.y},
		rect.br(),
		{rect.x, rect.y + rect.height}
	};
}

bool tryFindPage(cv::Mat inPage, cv::Mat& outPage, vector<cv::Point2f> srcQRCorners,
		cv::Size pageSize, cv::Rect2f qrBox) {
	// Estimate the transformation using the location from the QR code
	auto perspectiveTransform = cv::getPerspectiveTransform(srcQRCorners,
			rectCorners(qrBox));

	// Zoom out to include a larger area around the page to ensure we've got the whole page.
	double scaleFactor = 0.7;
	auto scale = cv::getRotationMatrix2D(
			{ pageSize.width / 2.0f, pageSize.height / 2.0f }, 0, scaleFactor);
	scale.push_back(cv::Mat::zeros(1, 3, CV_64F));
	scale.at<double>(2, 2) = 1.0;
	cv::Mat scalePerspective = scale * perspectiveTransform;
	cv::Mat warped;
	cv::warpPerspective(inPage, warped, scalePerspective, pageSize);

	// Blur to remove noise.
	cv::Mat blurred;
	cv::GaussianBlur(warped, blurred, { }, 1, 1);

	// Find edges
	cv::Mat edged;
	cv::Canny(blurred, edged, 75, 200);

	// Find contours.
	vector<vector<cv::Point>> contours { };
	cv::findContours(edged, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);

	// Sort contours by area
	std::sort(contours.begin(), contours.end(),
		[](vector<cv::Point> a, vector<cv::Point> b)
		{ return cv::contourArea(a) > cv::contourArea(b); });

	for (auto& contour : contours) {
		// Convert contours to polygon vertices.
		vector<cv::Point> corners { };
		cv::approxPolyDP(contour, corners, 0.05 * cv::arcLength(contour, true),
				true);

		// Look for 4-cornered shapes
		if (corners.size() == 4) {
			// Check that the area is large enough to likely be our box.
			double area = cv::contourArea(contour) / scaleFactor;
			if (area / pageSize.area() < 0.5) {
				break; // Area square too small.
			}

			// Order the points.
			sortPointsCW(corners);

			// Convert from Point to Point2f
			vector<cv::Point2f> corners2f;
			cv::Mat(corners).copyTo(corners2f);

			cv::Rect2f pageBox { { }, pageSize };
			vector<cv::Point2f> pageCorners = rectCorners(pageBox);

			// Compute the transform to the corners of the rectangle
			auto finalPerspective = cv::getPerspectiveTransform(corners2f, pageCorners);
			cv::warpPerspective(inPage, outPage, finalPerspective * scalePerspective, pageSize);

			cv::Mat mask;
			return true;
		}
	}

	// Failed to find corners.
	return false;
}

// Find all the shapes in an SVG file.
// Output is a map of SVG ID to shape
map<string, SVGShape> findSVGShapes(const string filename, cv::Size& outPageSize) {
	map<string, SVGShape> shapes;
	NSVGimage* image = nullptr;

	image = nsvgParseFromFile(filename.c_str(), "px", 150.0f);
	if (image == nullptr) {
		return {};
	}

	outPageSize = {static_cast<int>(image->width), static_cast<int>(image->height)};

	for (auto shape = image->shapes; shape != nullptr; shape = shape->next) {
		shapes[shape->id] = SVGShape(shape);
	}

	nsvgDelete(image);

	return shapes;
}

struct ScanResult {
	map<string, double> values;
	cv::Mat preview;
};

vector<ScanResult> scanImage(ImageScanner& scanner, const cv::Mat& rawImage,
		const cv::Size pageSize, const cv::Rect2f qrBox,
		const map<string, SVGShape>& shapes) {

	vector<ScanResult> results;

	// wrap image data
	Image zimage(rawImage.cols, rawImage.rows, "Y800", rawImage.data, rawImage.rows * rawImage.cols);

	// scan the image for barcodes
	scanner.scan(zimage);

	for (Image::SymbolIterator symbol = zimage.symbol_begin(); symbol != zimage.symbol_end(); ++symbol) {
		if (symbol->get_type() == ZBAR_QRCODE) {
			assert(symbol->get_location_size() == 4); // All QR codes have 4 corners
			vector<cv::Point2f> qrCorners {
				{ static_cast<float>(symbol->get_location_x(0)), static_cast<float>(symbol->get_location_y(0)) },
				{ static_cast<float>(symbol->get_location_x(3)), static_cast<float>(symbol->get_location_y(3)) },
				{ static_cast<float>(symbol->get_location_x(2)), static_cast<float>(symbol->get_location_y(2)) },
				{ static_cast<float>(symbol->get_location_x(1)), static_cast<float>(symbol->get_location_y(1)) }
			};

			map<string, double> bubbles;
			string data = symbol->get_data();

			cv::Mat warped;
			if (tryFindPage(rawImage, warped, qrCorners, pageSize, qrBox)) {
				cv::Mat preview;
				cv::cvtColor(warped, preview, cv::COLOR_GRAY2BGR);

				cv::Mat blurred;
				cv::GaussianBlur(warped, blurred, { }, 3, 3);

				cv::Mat thresholded;
				cv::threshold(blurred, thresholded, 0, 255,
						cv::THRESH_BINARY_INV | cv::THRESH_OTSU);

				{
					cv::Mat mask(thresholded.rows, thresholded.cols, thresholded.type(), 255);
					cv::rectangle(mask, {5, 5}, {mask.cols - 5, mask.rows - 5}, 0, -1, 0);

					double mean = cv::mean(thresholded, mask).val[0] / 255.0;
					if (mean < 0.75) {
						break;
					}
				}

				cv::Mat threshColor;
				cv::cvtColor(thresholded, threshColor, cv::COLOR_GRAY2BGR);

				vector<vector<cv::Point>> shapeVector(1);

				for (auto&& shape : shapes) {
					shapeVector[0] = shape.second.Outline;
					cv::Mat mask = cv::Mat::zeros(thresholded.rows, thresholded.cols, thresholded.type());
					cv::drawContours(mask, shapeVector, 0, 255, -1);

					double filled = cv::mean(thresholded, mask).val[0] / 255.0;

					{
						double green = (filled > 0.3 ? 1 : 0) * 255;
						double red = 255 - green;
						cv::drawContours(preview, shapeVector, -1, { 0, green, red }, 1);
					}
					{
						double green = (filled) * 255;
						double red = 255 - green;
						cv::drawContours(threshColor, shapeVector, -1, { 0, green, red }, 2);
					}
					bubbles[shape.first] = filled;
				}

				cv::Mat combinedView;
				cv::hconcat(preview, threshColor, combinedView);
				results.push_back(ScanResult { bubbles, combinedView });
			}
		}
	}

	// clean up
	zimage.set_data(NULL, 0);
	return results;
}

void printResult(map<string, double> result) {
	bool first = true;
	cout << "{";
	for (auto&& field : result) {
		if (first) {
			first = false;
		} else {
			cout << ',';
		}
		cout << '"' << field.first << "\":" << field.second;
	}
	cout << "}" << endl;
}

int main(int argc, char **argv) {
	if (argc < 3) {
		cout << "Usage: " << argv[0] << " SvgFile [ImageFile | CameraNumber]" << endl;
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

	cv::namedWindow(windowName, cv::WINDOW_NORMAL);
	cvSetWindowProperty(windowName, CV_WND_PROP_FULLSCREEN, CV_WINDOW_FULLSCREEN);

	cv::Size pageSize;
	map<string, SVGShape> shapes = findSVGShapes(svgFile, pageSize);

	if (shapes.count("qr") == 0) {
		cout << "Error: Could not find #qr in SVG" << endl;
		return -1;
	}

	// Find the QR code location in the SVG
	cv::Rect2f qrBox = shapes["qr"].BoundingBox;

	// Configure the QR code reader
	ImageScanner scanner { };
	scanner.set_config(ZBAR_QRCODE, ZBAR_CFG_ENABLE, 1);
	scanner.set_config(ZBAR_QRCODE, ZBAR_CFG_POSITION, 1);

	cv::Mat rawImage;
	cv::VideoCapture cap;

	if (liveCapture) {
		cap = cv::VideoCapture(camera); // open the default camera
		if (!cap.isOpened()) { // check if we succeeded
			cout << "Failed to open camera" << endl;
			return -1;
		}
		cap.set(CV_CAP_PROP_FRAME_WIDTH, 1080);
		cap.set(CV_CAP_PROP_FRAME_HEIGHT, 720);

		bool scanRequested = false;

		while (true) {
			int key;
			cv::Mat frame;
			cap >> frame; // get a new frame from camera
			cv::cvtColor(frame, rawImage, cv::COLOR_BGR2GRAY);

			imshow(windowName, rawImage);

			key = cv::waitKey(25);
			if (key == KEY_ESC || key == KEY_Q) { // ESC
				break;
			} else if (key == KEY_RIGHT) {
				scanRequested = true;
			}

			if (scanRequested) {
				auto results = scanImage(scanner, rawImage, pageSize, qrBox, shapes);

				if (results.size() > 0) {
					scanRequested = false;
				}

				for (auto&& result : results) {
					imshow(windowName, result.preview);
					if (cv::waitKey(0) == KEY_DOWN) {
						printResult(result.values);
					}
				}
			}
		}
	} else {
		rawImage = cv::imread(imageName.c_str(), cv::IMREAD_GRAYSCALE);
		if (rawImage.empty()) {
			cout << "Could not open or find the rawImage" << std::endl;
			return -1;
		}

		auto results = scanImage(scanner, rawImage, pageSize, qrBox, shapes);

		for (auto&& result : results) {
			imshow(windowName, result.preview);
			if (cv::waitKey(0) == KEY_RIGHT) {
				printResult(result.values);
			}
		}

		return results.size() > 0 ? 0 : -1;
	}

	return 0;
}
