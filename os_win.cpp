#include "os.hpp"

#include "log.hpp"

#include <Windows.h>
#include <Psapi.h>
#include <Pdh.h>

#include <vector>
#include <mutex>
#include <thread>

#define LOG CROSSOVER_MONITOR_LOG

using namespace std;

namespace crossover {
namespace monitor {
namespace client {
namespace os {

static unsigned process_count_helper(size_t max) noexcept {
	//This vector should always be of type DWORD, beware of the code below
	//that uses the size of DWORD to compute the max size of the internal
	//buffer if you change this!
	auto process_ids = std::make_unique<vector<DWORD>>(max);
	DWORD needed = 0;

	if (!EnumProcesses(process_ids->data(),
					   static_cast<DWORD>(process_ids->size() * sizeof(DWORD)),
					   &needed)) {
		LOG(error) << "Failed to enumerate processes, code: "
				   << GetLastError();
		return 0;
	}

	if (process_ids->size() * sizeof(DWORD) == needed) {
		//Increase the maximum number of processes and repeat the call.
		LOG(info) << "process_count_helper called with a maximum number of "
					 "processes too small (" << max << "), repeating the "
					 "call with a bigger limit (" << max * 2 << ")";
		process_ids.release();
		return process_count_helper(max * 2);
	}

	process_ids.release();
	return needed / sizeof(DWORD);
}

unsigned process_count() noexcept {
	return process_count_helper(1024 * 5);
}
float cpu_use_percent() noexcept {
	static PDH_HQUERY query;
	static PDH_HCOUNTER cpu_counter;

	static once_flag onceflag;
	call_once(onceflag, []() {
		PdhOpenQuery(NULL, NULL, &query);
		PdhAddEnglishCounter(query, 
							 L"\\Processor(_Total)\\% Processor Time", 
							 NULL, 
							 &cpu_counter);
		PdhCollectQueryData(query);
	});

	static mutex m;
	const lock_guard<mutex> guard(m);

	PDH_FMT_COUNTERVALUE value;
	PDH_STATUS status;
	if ((status = PdhCollectQueryData(query)) != ERROR_SUCCESS) {
		LOG(error) << "Error collecting CPU usage data, code: " << status;
		return 0;
	}
	if((status = PdhGetFormattedCounterValue(cpu_counter, 
											PDH_FMT_DOUBLE, 
											NULL,
											&value)) != ERROR_SUCCESS) {
		LOG(error) << "Error formatting CPU usage data, code: " << status;
		return 0;
	}
	return static_cast<float>(value.doubleValue);
}
float memory_use_percent() noexcept {
	MEMORYSTATUSEX mem;
	mem.dwLength = sizeof(mem);
	
	if (!GlobalMemoryStatusEx(&mem)) {
		LOG(error) << "Failed to get memory info, code: " << GetLastError();
		return 0;
	}

	const float available = 
		static_cast<float>(100 * mem.ullAvailPhys / mem.ullTotalPhys);
	const float used = 100 - available;
	return used;
}
void disk_iops() noexcept {
	HANDLE dev = CreateFile("\\\\.\\C:", 
        FILE_READ_ATTRIBUTES, 
        FILE_SHARE_READ | FILE_SHARE_WRITE, 
        NULL, 
        OPEN_EXISTING, 
        0, 
        NULL);

    DISK_PERFORMANCE disk_info { };
    DWORD bytes;

    if (dev == INVALID_HANDLE_VALUE) {
        std::cerr << "Error opening disk\n";
        return 1;
    }

    if (!DeviceIoControl(dev, 
            IOCTL_DISK_PERFORMANCE, 
            NULL, 
            0, 
            &disk_info, 
            sizeof(disk_info), 
            &bytes, 
            NULL))
    {
        std::cerr << "Failure in DeviceIoControl\n";
        return 1;
    }

    std::cout.imbue(std::locale(""));
    std::cout << "Bytes read: " << disk_info.BytesRead.QuadPart << "\n";
    std::cout << "Bytes written: " << disk_info.BytesWritten.QuadPart << "\n";
}

} //namespace os
} //namespace client
} //namespace monitor
} //namespace crossover

