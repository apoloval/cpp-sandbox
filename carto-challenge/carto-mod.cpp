/*

 ****************************************************************************
 *                                                                          *
 * see job description at: https://boards.greenhouse.io/cartodb/jobs/651069 *
 *                                                                          *
 ****************************************************************************


 * compile with:
 *   # g++-4.9 -O3 -std=c++11 carto.cpp -o torque
 * execute with:
 *   # ./torque tile.csv > image.ppm
 *
 * test.csv from here => https://drive.google.com/file/d/0B_oZluOoVpAnOFJ3WHI4SnpqcXM/view?usp=sharing
 * To validate everything is working just compare image.ppm with https://gist.githubusercontent.com/javisantana/d34c8eca63dafbe06434a141d045ebf6/raw/329b82e0f6c588d73b586c12af215c729006fd92/image.ppm
 * For young people, ppm is an image format, https://en.wikipedia.org/wiki/Netpbm_format
 *
 * We did this test internally and we found some problems when comparing the final images due to floating point issue. That's fine, this is the output we get using imagemagick compare tool:
        # compare -verbose -metric mae image.ppm image2.ppm diff.png
        Image: image.ppm
          Channel distortion: MAE
            gray: 0.0703125 (1.0729e-06)
            all: 0.0703125 (1.0729e-06)
        image.ppm=>diff.png PGM 256x256 256x256+0+0 16-bit sRGB 157c 40.8KB 0.000u 0:00.000
 */

#include <vector>
#include <iostream>
#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <thread>
#include <future>

using std::chrono::high_resolution_clock;
using std::chrono::duration_cast;
using std::chrono::milliseconds;
using std::memset;

namespace
{
    // tile bbox
    const float BBOX[] = { 4970241.3272153, -8257645.03970416,  5009377.08569731, -8218509.28122215 };
    // tile size in pixels
    const uint32_t pixel_resolution = 256;
    // resolution per in meters per pixel
    const float resolution = 152.874056570353;
    // helper
    const float resolution_inv = 1.0/resolution;
    const int grid_size = pixel_resolution * pixel_resolution;

    const int CONCURRENCY_LEVEL = 3;
};

struct row
{
    float x, y, amount;
};

struct grid_pixel
{
    float avg;
    uint32_t count;

    grid_pixel():
        avg(0.0f), count(0)
    {}
};

/**
 * reads a CSV file (well, space separated values) with this format:
 * amount x y
 */
void read(std::vector<row>& rows, const char* filename)
{
    std::ifstream file(filename);
    std::string line;
    while (std::getline(file, line))
    {
        row r;
        std::sscanf(line.c_str(), "%f %f %f", &r.amount, &r.y, &r.x);
        rows.push_back(r);
    }
}

void sequential_grid(
  std::vector<row>::const_iterator begin,
  std::vector<row>::const_iterator end,
  std::promise<std::vector<grid_pixel>> promise)
{
    std::vector<grid_pixel> hist;
    hist.resize(grid_size);

    for(auto it = begin; it != end; ++it)
    {
        const auto& r = *it;
        if (r.x > BBOX[0] && r.x < BBOX[2] && r.y > BBOX[1] && r.y < BBOX[3])
        {
            uint32_t x = resolution_inv * (r.x - BBOX[0]);
            uint32_t y = resolution_inv * (r.y - BBOX[1]);
            grid_pixel& px = hist[x * pixel_resolution + y];
            ++px.count;
            px.avg += r.amount;
        }
    }
    promise.set_value(hist);
}

/**
 * calculates 256x256 grid with avg values
 */
std::vector<grid_pixel> grid(const std::vector<row>& rows)
{
  std::vector<std::promise<std::vector<grid_pixel>>> result_promises(CONCURRENCY_LEVEL);
  std::vector<std::future<std::vector<grid_pixel>>> result_futures;
  for (auto& promise : result_promises) {
    result_futures.push_back(promise.get_future());
  }

  auto split_size = rows.size() / CONCURRENCY_LEVEL;
  std::vector<std::thread> workers(CONCURRENCY_LEVEL);
  for (int i = 0; i < CONCURRENCY_LEVEL; i++) {
    workers[i] = std::thread(sequential_grid,
                             rows.begin() + split_size * i,
                             rows.begin() + split_size * (i + 1),
                             std::move(result_promises[i]));
  }

  for (auto& worker : workers) {
    worker.join();
  }

  std::vector<std::vector<grid_pixel>> results;
  for (auto& future : result_futures) {
    results.push_back(future.get());
  }

  std::vector<grid_pixel> merged_result(grid_size);
  for (std::size_t i = 0; i < grid_size; i++) {
    auto& pixel = merged_result[i];
    for (auto& result : results) {
      pixel.count += result[i].count;
      pixel.avg += result[i].avg;
    }
    if (pixel.count) {
      pixel.avg /= pixel.count;
    }
  }

  return merged_result;
}

/**
 * writes a grid to a ppm file to stdout
 */
void write_ppm(const std::vector<grid_pixel>& grid) {

    std::cout << "P2" << std::endl;
    std::cout << "256 256" << std::endl;
    std::cout << "256" << std::endl;

    // calculate the max to normalize
    const auto& max_px = std::max_element(grid.begin(), grid.end(), [] (const grid_pixel& a, const grid_pixel& b) { return a.count*a.avg < b.count*b.avg; });
    float max = max_px->avg * max_px->count;

    for(int32_t x = pixel_resolution - 1; x >= 0; --x) {
        for(uint32_t y = 0; y < pixel_resolution; ++y) {
            const auto& px = grid[x * pixel_resolution + y];
            float avg = std::pow(px.avg*px.count/max, 0.4f);
            std::cout << 15 + uint32_t(avg*240) << " ";
        }
        std::cout << std::endl;
    }
}

int main (int argc, char** argv)
{

    if (argc != 2)
    {
        std::cerr << argv[0] << "file.csv" << std::endl;
        exit(-1);
    }

    std::vector<row> rows;
    // Reserve space to reduce the number of reallocations
    rows.reserve(10000000);

    // load rows, it will take some time, you do **not** need to optimize this part
    read(rows, argv[1]);

    std::vector<grid_pixel> g;
    for (int i = 0; i < 5; i++) {
      std::cerr << "Loaded " << rows.size() << "rows " << std::endl;
      high_resolution_clock::time_point t1 = high_resolution_clock::now();
      g = grid(rows);
      high_resolution_clock::time_point t2 = high_resolution_clock::now();
      std::cerr << "Time: " << duration_cast<milliseconds>(t2 - t1).count() << "ms" << std::endl;
    }

    write_ppm(g);
    return 0;
}
