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

Mat four_point_transform(Mat image, vector<Point2f> rect)
{
	int outWidth = 96;
	int outHeight = 96;

	vector<Point2f> dst{
		{0,0},
		{static_cast<float>(outWidth), 0},
		{static_cast<float>(outWidth), static_cast<float>(outHeight)},
		{0, static_cast<float>(outHeight)}
	};

	auto M = cv::getPerspectiveTransform(rect, dst);
	cout << M << endl;

	Mat warped;
	cv::warpPerspective(image, warped, M, {outWidth*6, outHeight*8});

	return warped;
}

int main (int argc, char **argv)
{
    string imageName("/home/arlo/git/scorescan-c/scorescan/src/scan.jpg"); // by default
    if( argc > 1)
    {
        imageName = argv[1];
    }
    Mat image;
    image = imread(imageName.c_str(), IMREAD_GRAYSCALE); // Read the file

    if(image.empty())
    {
        cout <<  "Could not open or find the image" << std::endl ;
        return -1;
    }

    // create a reader
    ImageScanner scanner{};

    // configure the reader
    scanner.set_config(ZBAR_QRCODE, ZBAR_CFG_ENABLE, 1);
    scanner.set_config(ZBAR_QRCODE, ZBAR_CFG_POSITION, 1);

    // wrap image data
    Image zimage(image.cols, image.rows, "Y800", image.data, image.rows * image.cols);

    // scan the image for barcodes
    scanner.scan(zimage);
    for(Image::SymbolIterator symbol = zimage.symbol_begin(); symbol != zimage.symbol_end(); ++symbol)
    {
        if (symbol->get_type() == ZBAR_QRCODE)
        {
        	assert(symbol->get_location_size() == 4);
        	vector<Point2f> points {
				{ static_cast<float>(symbol->get_location_x(0)), static_cast<float>(symbol->get_location_y(0)) },
				{ static_cast<float>(symbol->get_location_x(3)), static_cast<float>(symbol->get_location_y(3)) },
				{ static_cast<float>(symbol->get_location_x(2)), static_cast<float>(symbol->get_location_y(2)) },
				{ static_cast<float>(symbol->get_location_x(1)), static_cast<float>(symbol->get_location_y(1)) }
        	};

        	string data = symbol->get_data();

        	Mat warped = four_point_transform(image, points);

        	imshow( "Display window", warped );
        	waitKey(0);
        }
    }

    // clean up
    zimage.set_data(NULL, 0);

    return(0);
}
