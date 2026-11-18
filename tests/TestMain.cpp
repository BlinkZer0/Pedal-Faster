// Pedal Faster test runner.
//   PedalFasterTests                         run every test (exit code = number of failures, capped at 1)
//   PedalFasterTests --category DSP          run one category (DSP, Presets, Integration)
//   PedalFasterTests --regression-dir <dir>  where tests/regression/baseline.json lives
//   PedalFasterTests --calibrate <file.inc> | --export-factory <dir> | --screenshot <file.png>

#include "TestUtils.h"

juce::File gRegressionDir;
int runCalibration (const juce::File&);
int exportFactory (const juce::File&);
int renderScreenshot (const juce::File&);

struct StdoutLogger : juce::Logger
{
    void logMessage (const juce::String& m) override { std::puts (m.toRawUTF8()); std::fflush (stdout); }
};

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    StdoutLogger logger;
    juce::Logger::setCurrentLogger (&logger);
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (argv[i]);

    auto argAfter = [&args] (const juce::String& flag) -> juce::String
    {
        const int i = args.indexOf (flag);
        return i >= 0 && i + 1 < args.size() ? args[i + 1] : juce::String();
    };
    auto absolute = [] (const juce::String& path) { return juce::File::getCurrentWorkingDirectory().getChildFile (path); };

    if (args.contains ("--calibrate"))       return runCalibration (absolute (argAfter ("--calibrate")));
    if (args.contains ("--export-factory"))  return exportFactory (absolute (argAfter ("--export-factory")));
    if (args.contains ("--screenshot"))      return renderScreenshot (absolute (argAfter ("--screenshot")));

    gRegressionDir = args.contains ("--regression-dir") ? absolute (argAfter ("--regression-dir"))
                                                        : juce::File::getCurrentWorkingDirectory().getChildFile ("tests/regression");

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.setPassesAreLogged (false);
    const auto category = argAfter ("--category");
    if (category.isNotEmpty()) runner.runTestsInCategory (category);
    else runner.runAllTests();

    int failures = 0, passes = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        const auto* r = runner.getResult (i);
        failures += r->failures; passes += r->passes;
    }
    std::printf ("\n==== %d checks passed, %d failed ====\n", passes, failures);
    juce::Logger::setCurrentLogger (nullptr);
    return failures > 0 ? 1 : 0;
}
