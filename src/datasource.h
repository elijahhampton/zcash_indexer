#include <function>

class DataSource {
    public:
        template <typename T&>
        using DataCallback = std::function<void(const T&)>;
    protected:
        DataCallback dataCallback;
        void setDataCallback(DataCallback callback) {
            dataCallback = std::move(callback);
        }
};