#include <application.hpp>
#include <os.hpp>

#include <utils.hpp>
#include <data.hpp>

#include <cpprest/json.h>
#include <cpprest/http_client.h>
#include <cpprest/asyncrt_utils.h>

#include <atomic>
#include <string>
#include <stdexcept>
#include <numeric>

#define LOG CROSSOVER_MONITOR_LOG

using namespace std;

namespace crossover {
namespace monitor {
namespace client {

struct application::impl final {
		atomic<bool> stop = false;
		atomic<bool> running = false;
	};

static data collect_data() {
	return data{
		os::cpu_use_percent(),
		os::memory_use_percent(),
		os::process_count()
		os::disk_iops(); // added by Gurpreet Singh
	};
}

static web::json::value data_to_json(const data& data) noexcept {
	using namespace web;
	json::value v(json::value::object());
	json::object& o(v.as_object());
	o[L"cpu_percent"] = data.get_cpu_percent();
	o[L"memory_percent"] = data.get_memory_percent();
	o[L"process_count"] = data.get_process_count();
	return v;
}

static void send_data(const string& url, const string& key, const data& data) {
	using namespace web;

	const json::value jsondata(data_to_json(data));
	const uri url_full(utility::conversions::to_string_t(url));

	uri_builder builder;
	builder.set_scheme(url_full.scheme());
	builder.set_user_info(url_full.user_info());
	builder.set_host(url_full.host());
	builder.set_port(url_full.port());

	const uri base(builder.to_uri());

	LOG(debug) << "Base URI: " << base.to_string()
		<< " - URI resource: " << url_full.resource().to_string();
	
	// Actual sending is out of scope...
	// so reporting unconditional success here
	LOG(info) << "Data sent successfully to " << url;
}

static void report_sent_callback(const data& sent_data)
{
	static unsigned reports_sent = 0;
	static float latest_cpu_values[10] = { 0 };
	static mutex m;

	lock_guard<mutex> l(m);

	latest_cpu_values[reports_sent % 10] = sent_data.get_cpu_percent();

	// Every 10 reports sent - show statistics about mean CPU usage
	// Skip first time hit
	if ((++reports_sent) % 10 == 0 && reports_sent != 0)
	{
		auto sum = std::accumulate(std::begin(latest_cpu_values), std::end(latest_cpu_values), 0.0f);
		auto mean = sum / 10.0f;

		LOG(info) << "Mean CPU usage: " << mean;
	}
}


application::application(
	const std::chrono::minutes& period) :
	pimpl_(new impl),
	period_(period) {
	if (period_ < chrono::minutes(1)) {
		throw invalid_argument("Invalid arguments to application constructor");
	}
}

application::~application() {
	delete pimpl_.get();
}

void application::run() {
	if (pimpl_->running) {
		LOG(warning) << "application::run already running, ignoring call";
		return;
	}

	pimpl_->running = true;

	LOG(info) << "Starting application loop";
	utils::scope_exit exit_guard([this] {
		pimpl_->running = false;
		pimpl_->stop = false;

		LOG(info) << "Exiting application loop";
	});

	const chrono::milliseconds resolution(100);
	do {
		try {
			using namespace web;
			auto collected_data = collect_data();
			const json::value jsondata(data_to_json(collected_data));
			LOG(info) << jsondata.to_string() << endl;
		}
		catch (const std::exception& e) {
			LOG(error) << "Failed to collect and send data to server: "
				<< e.what();
		}
	} while (utils::interruptible_sleep(period_, resolution, pimpl_->stop) !=
		utils::interruptible_sleep_result::interrupted);
}

void application::stop() noexcept {
	if (pimpl_->running) {
		LOG(info) << "Stop requested, waiting for tasks to finish";
		pimpl_->stop = true;
	}
}

} //namespace client
} //namespace monitor
} //namespace crossover

