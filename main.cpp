#include <iostream>
#include <fstream>
#include <vector>
#include <thread>
#include <mutex>

#define WIDTH 1920
#define HEIGHT 1080

using namespace std;

// Structure to represent an RGB pixel
struct RGB {
    uint8_t r, g, b;
};

// Structure to represent a YUV pixel
struct YUV {
    uint8_t y, u, v;
};

// Function to read BMP file
void readBMP(const string& filename, vector<RGB>& data, uint32_t& width, uint32_t& height) {
    ifstream file(filename, ios::binary);
    if (!file) {
        cerr << "Error opening BMP file: " << filename << endl;
        exit(1);
    }

    // Reading BMP header
    uint8_t header[54];
    file.read(reinterpret_cast<char*>(header), 54);

    width = *reinterpret_cast<uint32_t*>(&header[18]);
    height = *reinterpret_cast<uint32_t*>(&header[22]);

    // Reading BMP data
    data.resize(width * height);
    for (int32_t i = height - 1; i >= 0; --i) { // BMP data is stored bottom-to-top
        for (uint32_t j = 0; j < width; ++j) {
            uint8_t b = file.get();
            uint8_t g = file.get();
            uint8_t r = file.get();
            data[i * width + j] = { r, g, b };
        }
    }
}

// Function to convert RGB to YUV420
void rgbToYUV420(const vector<RGB>& rgbData, vector<YUV>& yuvData, uint32_t width, uint32_t height) {
    yuvData.resize(width * height);

    auto worker = [&](uint32_t start, uint32_t end) {
        for (uint32_t i = start; i < end; ++i) {
            const RGB& rgb = rgbData[i];
            YUV& yuv = yuvData[i];

            yuv.y = static_cast<uint8_t>(0.299 * rgb.r + 0.587 * rgb.g + 0.114 * rgb.b);
            yuv.u = static_cast<uint8_t>((-0.16874 * rgb.r - 0.33126 * rgb.g + 0.5 * rgb.b) + 128);
            yuv.v = static_cast<uint8_t>((0.5 * rgb.r - 0.41869 * rgb.g - 0.08131 * rgb.b) + 128);
        }
    };

    // Create threads for parallel conversion
    uint32_t numThreads = thread::hardware_concurrency();
    vector<thread> threads;
    uint32_t step = (width * height) / numThreads;

    for (uint32_t i = 0; i < numThreads; ++i) {
        uint32_t start = i * step;
        uint32_t end = (i == numThreads - 1) ? (width * height) : (start + step);
        threads.emplace_back(worker, start, end);
    }

    for (auto& t : threads) {
        t.join();
    }
}

// Function to overlay the BMP onto YUV420 video frames
void overlayBMP(vector<YUV>& yuvFrame, const vector<YUV>& bmpYUV, uint32_t frameWidth, uint32_t frameHeight, uint32_t bmpWidth, uint32_t bmpHeight) {
    for (uint32_t y = 0; y < bmpHeight; ++y) {
        for (uint32_t x = 0; x < bmpWidth; ++x) {
            uint32_t frameIndex = y * frameWidth + x;
            uint32_t bmpIndex = y * bmpWidth + x;
            yuvFrame[frameIndex] = bmpYUV[bmpIndex];
        }
    }
}

int main() {
    // Open YUV file
    ifstream yuvFile("input.yuv", ios::binary);
    ofstream outputYuvFile("output.yuv", ios::binary);

    if (!yuvFile || !outputYuvFile) {
        cerr << "Error opening YUV files." << endl;
        return 1;
    }

    // Read BMP file
    vector<RGB> bmpData;
    uint32_t bmpWidth, bmpHeight;
    readBMP("input.bmp", bmpData, bmpWidth, bmpHeight);

    // Convert BMP to YUV
    vector<YUV> bmpYUV;
    rgbToYUV420(bmpData, bmpYUV, bmpWidth, bmpHeight);

    // Process YUV frames
    vector<uint8_t> yPlane(WIDTH * HEIGHT);
    vector<uint8_t> uPlane((WIDTH / 2) * (HEIGHT / 2));
    vector<uint8_t> vPlane((WIDTH / 2) * (HEIGHT / 2));

    while (yuvFile.read(reinterpret_cast<char*>(yPlane.data()), yPlane.size())) {
        yuvFile.read(reinterpret_cast<char*>(uPlane.data()), uPlane.size());
        yuvFile.read(reinterpret_cast<char*>(vPlane.data()), vPlane.size());

        // Convert YUV planes to YUV structure for easier manipulation
        vector<YUV> frame(WIDTH * HEIGHT);
        for (uint32_t y = 0; y < HEIGHT; ++y) {
            for (uint32_t x = 0; x < WIDTH; ++x) {
                uint32_t index = y * WIDTH + x;
                frame[index].y = yPlane[index];
                frame[index].u = uPlane[(y / 2) * (WIDTH / 2) + (x / 2)];
                frame[index].v = vPlane[(y / 2) * (WIDTH / 2) + (x / 2)];
            }
        }

        // Overlay BMP onto YUV frame
        overlayBMP(frame, bmpYUV, WIDTH, HEIGHT, bmpWidth, bmpHeight);

        // Write output frame
        for (uint32_t y = 0; y < HEIGHT; ++y) {
            for (uint32_t x = 0; x < WIDTH; ++x) {
                outputYuvFile.put(frame[y * WIDTH + x].y);
            }
        }
        for (uint32_t y = 0; y < HEIGHT; y += 2) {
            for (uint32_t x = 0; x < WIDTH; x += 2) {
                outputYuvFile.put(frame[y * WIDTH + x].u);
            }
        }
        for (uint32_t y = 0; y < HEIGHT; y += 2) {
            for (uint32_t x = 0; x < WIDTH; x += 2) {
                outputYuvFile.put(frame[y * WIDTH + x].v);
            }
        }
    }

    cout << "Program finished successfully." << endl;
    cin.ignore();
    cin.get();

    return 0;
}