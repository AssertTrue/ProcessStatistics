// ProcessStatistics.cpp : main project file.

#include <list>
#include <Windows.h>
#include <psapi.h>
#undef max
#include <algorithm>

using namespace System;
using namespace System::Diagnostics;
using namespace System::Text;
using namespace System::Threading;

struct ProcessStatistics
{
    ProcessStatistics(const double & aTotalProcessorTimeInSeconds,
                      const double & aPeakWorkingSetInKB,
                      const double & aPeakPageFileUsageInKB)
        : totalProcessorTimeInSeconds(aTotalProcessorTimeInSeconds),
          peakWorkingSetInKB(aPeakWorkingSetInKB),
          peakPageFileUsageInKB(aPeakPageFileUsageInKB) {}

    const double totalProcessorTimeInSeconds;
    const double peakWorkingSetInKB;
    const double peakPageFileUsageInKB;
};

public ref class OutputCollector
{
public:

    OutputCollector(Process ^ aProcess)
    {
        this->OutputWaitHandle = gcnew AutoResetEvent(false);
        this->ErrorWaitHandle = gcnew AutoResetEvent(false);
        this->Output= gcnew StringBuilder();
        this->Error= gcnew StringBuilder();

        aProcess->OutputDataReceived += gcnew DataReceivedEventHandler(this, &OutputCollector::onOutputDataReceived);
        aProcess->ErrorDataReceived += gcnew DataReceivedEventHandler(this, &OutputCollector::onErrorDataReceived);
    }

    AutoResetEvent^ OutputWaitHandle;
    AutoResetEvent^ ErrorWaitHandle;
    StringBuilder ^ Output;
    StringBuilder ^ Error;

private:

    void onOutputDataReceived(Object ^ aObject, DataReceivedEventArgs ^ aArguments)
    {
        if (aArguments->Data == nullptr)
        {
            this->OutputWaitHandle->Set();
        }
        else
        {
            this->Output->AppendLine(aArguments->Data);
        }
    }

    void onErrorDataReceived(Object ^ aObject, DataReceivedEventArgs ^ aArguments)
    {
        if (aArguments->Data == nullptr)
        {
            this->ErrorWaitHandle->Set();
        }
        else
        {
            this->Error->AppendLine(aArguments->Data);
        }
    }

};

ProcessStatistics runProcess(System::String ^ aApplication, System::String ^ aArguments)
{
    Process ^ process = gcnew Process();
    process->StartInfo->FileName= aApplication;
    process->StartInfo->Arguments = aArguments;
    process->StartInfo->UseShellExecute = false;
    process->StartInfo->CreateNoWindow = true;
    process->StartInfo->RedirectStandardError = true;
    process->StartInfo->RedirectStandardOutput = true;

    OutputCollector ^ outputCollector= gcnew OutputCollector(process);

    process->Start();

    process->BeginOutputReadLine();
    process->BeginErrorReadLine();

    SIZE_T peakWorkingSetSize= 0;
    SIZE_T peakPageFileUsage= 0;

    do
    {
        if (!process->HasExited)
        {
            PROCESS_MEMORY_COUNTERS memoryCounters;

            GetProcessMemoryInfo((HANDLE)process->Handle, 
                                 &memoryCounters,
                                 sizeof(PROCESS_MEMORY_COUNTERS));

            peakWorkingSetSize= memoryCounters.PeakWorkingSetSize;
            peakPageFileUsage= memoryCounters.PeakPagefileUsage;
        }
    } while (!process->WaitForExit(1000)
             || !outputCollector->ErrorWaitHandle->WaitOne(1000)
             || !outputCollector->OutputWaitHandle->WaitOne(1000));

    float BytesToKB = 1024;

    ProcessStatistics processStatistics(process->TotalProcessorTime.TotalSeconds,
                                        peakWorkingSetSize / BytesToKB,
                                        peakPageFileUsage / BytesToKB);

    Console::WriteLine("Standard Output:");
    Console::WriteLine(outputCollector->Output->ToString());
    Console::WriteLine("Standard Error:");
    Console::WriteLine(outputCollector->Error->ToString());
    Console::WriteLine(L"Total processor time (s): " + processStatistics.totalProcessorTimeInSeconds.ToString());
    Console::WriteLine(L"Peak working set (kb): " + processStatistics.peakWorkingSetInKB.ToString());
    Console::WriteLine(L"Peak page file usage (kb): " + processStatistics.peakPageFileUsageInKB.ToString());
    Console::WriteLine("");

    return processStatistics;
}

template<typename T>
class Statistic
{
public:

    void add(T aValue)
    {
        values.push_back(aValue);
    }

    T average() const
    {
        T total= 0;

        for (auto valueIterator= this->values.begin();
             valueIterator != this->values.end();
             ++valueIterator)
        {
            total += *valueIterator;
        }

        return total / static_cast<T>(this->values.size());
    }

    T standardDeviation() const
    {
        T total= 0;
        T averageValue= this->average();

        for (auto valueIterator= this->values.begin();
             valueIterator != this->values.end();
             ++valueIterator)
        {
            T difference= (*valueIterator) - averageValue;
            total += difference*difference;
        }

        return sqrt(total / static_cast<T>(std::max<int>(((int)this->values.size())-1,1)));
    }

private:

    std::list<T> values;
};

void runJobs(String ^ aApplicationPath, String ^ aArguments, size_t aNumberOfRuns, String ^ aOutputFileName, String ^ aRunID)
{
    Statistic<double> totalProcessorTimeInSecondsStatistic;
    Statistic<double> peakWorkingSetInKBStatistic;
    Statistic<double> peakPageFileUsageInKBStatistic; 
    
    for (size_t runNumber= 1; runNumber <= aNumberOfRuns; ++runNumber)
    {
        Console::WriteLine(L"Starting run number " + runNumber.ToString());
        try
        {
            ProcessStatistics processStatistics= runProcess(aApplicationPath, aArguments);
            totalProcessorTimeInSecondsStatistic.add(processStatistics.totalProcessorTimeInSeconds);
            peakWorkingSetInKBStatistic.add(processStatistics.peakWorkingSetInKB);
            peakPageFileUsageInKBStatistic.add(processStatistics.peakPageFileUsageInKB);
        }
        catch (System::ComponentModel::Win32Exception ^ aException)
        {
            Console::WriteLine(L"Run failed: " + aException->Message);
        }
    }

    Console::WriteLine(L"All runs complete.\n");
    Console::WriteLine(L"Average total processor time (s): " + totalProcessorTimeInSecondsStatistic.average().ToString());
    Console::WriteLine(L"Standard deviation of total processor time (s): " + totalProcessorTimeInSecondsStatistic.standardDeviation().ToString());

    Console::WriteLine(L"Average peak working set (kb): " + peakWorkingSetInKBStatistic.average().ToString());
    Console::WriteLine(L"Standard deviation of peak working set (kb): " + peakWorkingSetInKBStatistic.standardDeviation().ToString());

    Console::WriteLine(L"Average peak page file usage (kb): " + peakPageFileUsageInKBStatistic.average().ToString());
    Console::WriteLine(L"Standard deviation of peak page file usage (kb): " + peakPageFileUsageInKBStatistic.standardDeviation().ToString());

    Console::WriteLine(L"\nResults will be written to " + aOutputFileName);

    String ^ currentContent= System::String::Empty;

    String ^ data= totalProcessorTimeInSecondsStatistic.average().ToString()
                   + ", " + totalProcessorTimeInSecondsStatistic.standardDeviation().ToString()
                   + ", " + peakWorkingSetInKBStatistic.average().ToString()
                   + ", " + peakWorkingSetInKBStatistic.standardDeviation().ToString()
                   + ", " + peakPageFileUsageInKBStatistic.average().ToString()
                   + ", " + peakPageFileUsageInKBStatistic.standardDeviation().ToString()
                   + ", " + aRunID
                   + Environment::NewLine;

    if (!IO::File::Exists(aOutputFileName))
    {
        data= "Average total processor time (s), Standard deviation of total processor time (s), Average peak working set (kb), Standard deviation of peak working set (kb), Average peak page file usage (kb), Standard deviation of peak page file usage (kb), Run ID" + Environment::NewLine
              + data;
    }

    IO::File::AppendAllText(aOutputFileName, data);
}

int main(array<System::String ^> ^args)
{
    size_t numberOfRuns= 0;

    if (args->Length >= 4
        && size_t::TryParse(args[0], numberOfRuns)
        && args[1]->Length > 0
        && args[2]->Length > 0)
    {
        size_t numberOfRuns = size_t::Parse(args[0]);

        String ^ app = args[3];
    
        String ^ arguments = "";
    
        for (int argumentIndex= 4; argumentIndex < args->Length; ++argumentIndex)
        {
            arguments = arguments + args[argumentIndex] + " ";
        }

        runJobs(app, arguments, numberOfRuns, args[1], args[2]);
    }
    else
    {
        Console::WriteLine("Usage: ProcessStatistics.exe <number of jobs> <path to output file> <run id> <path to executable> [argument1] [argument2] ...");
    }

    return 0;
}


