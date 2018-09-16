#include "application.hpp"

#include "log.hpp"
#include "os.hpp"

#include <boost/program_options.hpp>

#include <cstdlib>
#include <stdexcept>
#include <iostream>
#include <string>
#include <chrono>

using namespace std;
using namespace crossover::monitor;
namespace po = boost::program_options;

#define LOG CROSSOVER_MONITOR_LOG

int main(int argc, char* argv[]) {
	log::init();	
	LOG(info) << "Crossover Monitor Client Started";
	po::options_description description;
	description.add_options()
		("help", "Show this message")
		("minutes", po::value<unsigned>()->default_value(5), "Period between reports in minutes")
		("logfile", po::value<string>(), "Log file");

	po::variables_map vm;
	try {
		po::store(po::parse_command_line(argc, argv, description), vm);
	} catch (const exception& e) {
		LOG(error) << "Error while parsing command line: " << e.what();
		cout << description << endl;
		return EXIT_FAILURE;
	}

	if (vm.count("help")) {
		cout << description << endl;
		return EXIT_SUCCESS;
	}

	try {
		po::notify(vm);
	} catch (const po::required_option& e) {
		LOG(error) << "Missing required option: " << e.what();
		cout << description << endl;
		return EXIT_FAILURE;
	}

	if (vm.count("logfile")) {
		log::set_file(vm["logfile"].as<string>());
	}

	try {		
		client::application app(chrono::minutes(vm["minutes"].as<unsigned>()));
		
		os::set_termination_handler([&app]() {
			try {
				app.stop();
			} catch (const std::exception& e) {
				LOG(error) << e.what();
			}
		});

		app.run();
	} catch (const std::exception& e) {
		LOG(error) << e.what();
		return EXIT_FAILURE;
	} catch (...) {
		LOG(error) << "Unknown exception, exiting";
		return EXIT_FAILURE;
	}

	LOG(info) << "Exiting gracefully";

	return EXIT_SUCCESS;
}

