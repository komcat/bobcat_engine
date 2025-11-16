// OpenCVTest.h
#pragma once

#ifdef HAS_OPENCV

#include <string>
#include <opencv2/core.hpp>

/**
 * @brief Simple OpenCV integration test class
 *
 * Tests basic OpenCV functionality:
 * - Mat creation and manipulation
 * - Image processing operations
 * - File I/O (optional)
 */
class OpenCVTest
{
public:
    OpenCVTest() = default;
    ~OpenCVTest() = default;

    /**
     * @brief Run all OpenCV tests
     * @return true if all tests pass
     */
    bool runAllTests();

    /**
     * @brief Test basic cv::Mat operations
     * @return true if test passes
     */
    bool testMatOperations();

    /**
     * @brief Test color space conversion
     * @return true if test passes
     */
    bool testColorConversion();

    /**
     * @brief Test image filtering (blur, edge detection)
     * @return true if test passes
     */
    bool testImageFiltering();

    /**
     * @brief Get OpenCV version info
     * @return Version string
     */
    std::string getVersionInfo() const;

    /**
     * @brief Get last error message
     * @return Error message or empty string
     */
    std::string getLastError() const { return m_lastError; }

private:
    std::string m_lastError;
};

#endif // HAS_OPENCV
