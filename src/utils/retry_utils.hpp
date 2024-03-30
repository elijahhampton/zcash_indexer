#include <chrono>
#include <thread>
#include <cmath>
#include <functional>
#include <stdexcept>

namespace Utils
{
    template <typename ReturnType, typename... Args>
    ReturnType ExponentialBackoffRetryBaseN(std::function<ReturnType(Args...)> func, uint8_t max_retries, Args... args)
    {
        uint8_t retry_count = 0;
        const uint8_t max_retry_count = max_retries;
        const uint8_t baseDelay = 100;
        const uint max_delay = 5000;

        while (retry_count < max_retry_count)
        {
            try
            {
                return func(args...);
            }
            catch (const std::exception &e)
            {
                if (retry_count >= max_retry_count - 1)
                {
                    // Rethrow the exception if the max retry count has been met.
                    throw;
                }

                // Calculate the next delay for the function with an exponential backoff.
                // The delay is increased by a multiple of 2 each iteration
                // Example:
                // For retry_count = 0, the delay is baseDelay * 2^0 = baseDelay
                // For retry_count = 1, the delay is baseDelay * 2^1 = 2 * baseDelay
                // For retry_count = 2, the delay is baseDelay * 2^2 = 4 * baseDelay
                uint delay = baseDelay * std::pow(2, retry_count);
                delay = std::min(delay, max_delay);

                // Wait for the next retry.
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
                ++retry_count;
            }
        }

        throw std::runtime_error("Retry limit exceeded")
    }
}
