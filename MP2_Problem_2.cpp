#include <condition_variable>
#include <iostream>
#include <thread>
#include <mutex>
#include <map>
#include <shared_mutex>
#include <string>
#include <vector>
#include <atomic>
#include <set>
#include <chrono>
using namespace std;

struct Appointment {
    int id;
    int patientId;
    string datetime;
    string reason;
};

struct Patient {
    int id;
    string name;
    int age;
};

struct Record {
    int patientId;
    string patientName;
    int patientAge;
    vector<string> entries;
};

class LockMonitor {
public:
    atomic<bool> patientLock{false};
    atomic<bool> appointmentLock{false};
    atomic<bool> recordLock{false};

    void displayLockStatus() {
        cout << "\n--- Lock Status ---\n";
        cout << "Patient Lock: " << (patientLock ? "LOCKED" : "UNLOCKED") << "\n";
        cout << "Appointment Lock: " << (appointmentLock ? "LOCKED" : "UNLOCKED") << "\n";
        cout << "Record Lock: " << (recordLock ? "LOCKED" : "UNLOCKED") << "\n";
    }

    void checkDeadlocks() {
        cout << "\n--- Deadlock Check ---\n";
        // Simulation only
        if (patientLock && appointmentLock && recordLock) {
            cout << "⚠️  Potential deadlock: all resources are locked!\n";
        } else {
            cout << "No deadlocks detected.\n";
        }
    }
};

LockMonitor lockMonitor;

class PatientManager {
    private:
        map<int, Patient> patients;
        shared_mutex patientMutex;
        int nextPatientId = 0;

    public:
        void registerPatient(const string& name, int age) {
            lockMonitor.patientLock = true;
            unique_lock lock(patientMutex);
            int id = ++nextPatientId;
            patients[id] = {id, name, age};
            cout << "Patient registered with ID " << id << ": " << name << "\n";
            lockMonitor.patientLock = false;
        }

        void updatePatient(int id, const string& name, int age) {
            lockMonitor.patientLock = true;
            unique_lock lock(patientMutex);
            if (patients.find(id) != patients.end()) {
                patients[id] = {id, name, age};
                cout << "Patient updated: " << name << "\n";
            } else {
                cout << "Patient not found.\n";
            }
            lockMonitor.patientLock = false;
        }

        void removePatient(int id) {
            lockMonitor.patientLock = true;
            unique_lock lock(patientMutex);
            if (patients.erase(id)) {
                cout << "Patient removed.\n";
            } else {
                cout << "Patient not found.\n";
            }
            lockMonitor.patientLock = false;
        }

        void listPatients() {
            lockMonitor.patientLock = true;
            shared_lock lock(patientMutex);
            for (const auto& [id, patient] : patients) {
                cout << "ID: " << id << ", Name: " << patient.name << ", Age: " << patient.age << "\n";
            }
            lockMonitor.patientLock = false;
        }
};

class AppointmentManager {
    private:
        map<int, Appointment> appointments;
        mutex appMutex;
        condition_variable_any appointmentNotifier;
        int nextAppointmentId = 0;

    public:
        void scheduleAppointment(int patientId, const string& datetime, const string& reason) {
            lockMonitor.appointmentLock = true;
            unique_lock lock(appMutex);
            int id = ++nextAppointmentId;
            appointments[id] = {id, patientId, datetime, reason};
            cout << "Appointment scheduled with ID " << id << ".\n";
            appointmentNotifier.notify_all();
            lockMonitor.appointmentLock = false;
        }

        void updateAppointment(int id, const string& newDatetime, const string& newReason) {
            lockMonitor.appointmentLock = true;
            unique_lock lock(appMutex);
            if (appointments.find(id) != appointments.end()) {
                appointments[id].datetime = newDatetime;
                appointments[id].reason = newReason;
                cout << "Appointment updated.\n";
            } else {
                cout << "Appointment not found.\n";
            }
            lockMonitor.appointmentLock = false;
        }

        void cancelAppointment(int id) {
            lockMonitor.appointmentLock = true;
            unique_lock lock(appMutex);
            if (appointments.erase(id)) {
                cout << "Appointment canceled.\n";
            } else {
                cout << "Appointment not found.\n";
            }
            lockMonitor.appointmentLock = false;
        }

        void listAppointments() {
            lockMonitor.appointmentLock = true;
            unique_lock lock(appMutex);
            for (const auto& [id, appt] : appointments) {
                cout << "ID: " << id << ", Patient ID: " << appt.patientId
                     << ", DateTime: " << appt.datetime << ", Reason: " << appt.reason << "\n";
            }
            lockMonitor.appointmentLock = false;
        }
};

class RecordManager {
    private:
        map<int, Record> records;
        mutex recordMutex;

    public:
        void addRecord(int patientId, const string& name, int age) {
            lockMonitor.recordLock = true;
            unique_lock lock(recordMutex);
            if (records.find(patientId) == records.end()) {
                records[patientId] = {patientId, name, age, {}};
                cout << "Record created for Patient ID " << patientId << ".\n";
            } else {
                cout << "Record already exists for this patient.\n";
            }
            lockMonitor.recordLock = false;
        }

        void updateRecord(int patientId, const string& entry) {
            lockMonitor.recordLock = true;
            unique_lock lock(recordMutex);
            if (records.find(patientId) != records.end()) {
                records[patientId].entries.push_back(entry);
                cout << "Medical record updated for Patient ID " << patientId << ".\n";
            } else {
                cout << "No record found. Add one first.\n";
            }
            lockMonitor.recordLock = false;
        }

        void viewRecord(int patientId) {
            lockMonitor.recordLock = true;
            unique_lock lock(recordMutex);
            if (records.find(patientId) != records.end()) {
                const auto& r = records[patientId];
                cout << "Record for Patient ID " << patientId << ":\n";
                cout << "Name: " << r.patientName << ", Age: " << r.patientAge << "\n";
                cout << "Entries:\n";
                for (const auto& entry : r.entries) {
                    cout << "- " << entry << "\n";
                }
            } else {
                cout << "No records found for this patient.\n";
            }
            lockMonitor.recordLock = false;
        }
};

void patientMenu() {
    cout << "\n--- Patient Management Menu ---\n";
    cout << "1. Register Patient\n";
    cout << "2. Update Patient\n";
    cout << "3. Remove Patient\n";
    cout << "4. List Patients\n";
    cout << "0. Back to Main Menu\n";
    cout << "Choose an option: ";
}

void appointmentMenu() {
    cout << "\n--- Appointment Management Menu ---\n";
    cout << "1. Schedule Appointment\n";
    cout << "2. Update Existing Appointment\n";
    cout << "3. Remove Existing Appointment\n";
    cout << "4. List Appointments\n";
    cout << "0. Back to Main Menu\n";
    cout << "Choose an option: ";
}

void recordMenu() {
    cout << "\n--- Recording Management Menu ---\n";
    cout << "1. Add Record\n";
    cout << "2. Update Record\n";
    cout << "3. View Records\n";
    cout << "0. Back to main menu.\n";
    cout << "Choose an option: ";
}

void menu() {
    cout << "\n--- Hospital Management Menu ---\n";
    cout << "1. Patient Management\n";
    cout << "2. Appointment Management\n";
    cout << "3. Record Management\n";
    cout << "4. Concurrency Control\n";
    cout << "5. Check Deadlocks\n";
    cout << "0. Exit\n";
    cout << "Choose an option: ";
}

int main() {
    PatientManager pm;
    AppointmentManager am;
    RecordManager rm;
    int mainChoice = -1;

    while (mainChoice != 0) {
        menu();
        cin >> mainChoice;

        if (mainChoice == 1) { // Patient Management
            int patientChoice = -1;
            while (patientChoice != 0) {
                patientMenu();
                cin >> patientChoice;

                int id, age;
                string name;

                if (patientChoice == 1) {
                    cout << "Enter Name: ";
                    cin.ignore(); // Ignore "1" from choosing option 1
                    string name;
                    getline(cin, name);

                    cout << "Enter Age: ";
                    int age;
                    cin >> age;

                    pm.registerPatient(name, age);
                } else if (patientChoice == 2) {
                    cout << "Enter ID, New Name, New Age: ";
                    cin >> id >> name >> age;
                    pm.updatePatient(id, name, age);
                } else if (patientChoice == 3) {
                    cout << "Enter ID to remove: ";
                    cin >> id;
                    pm.removePatient(id);
                } else if (patientChoice == 4) {
                    pm.listPatients();
                } else if (patientChoice == 0) {
                    cout << "Returning to main menu...\n";
                } else {
                    cout << "Invalid choice.\n";
                }
            }
        } else if (mainChoice == 2) {
            int appointmentChoice = -1;
            while (appointmentChoice != 0) {
                appointmentMenu();
                cin >> appointmentChoice;

                if (appointmentChoice == 1) {
                    int patientId;
                    cout << "Enter Patient ID: ";
                    cin >> patientId;
                    cin.ignore();
                    string date, reason;
                    cout << "Enter Appointment Date: ";
                    getline(cin, date);
                    cout << "Enter Reason: ";
                    getline(cin, reason);

                    am.scheduleAppointment(patientId, date, reason);
                } else if (appointmentChoice == 2) {
                    int id;
                    cout << "Enter Appointment ID: ";
                    cin >> id;
                    cin.ignore();
                    string newDate, newReason;
                    cout << "Enter New Date: ";
                    getline(cin, newDate);
                    cout << "Enter New Reason: ";
                    getline(cin, newReason);
                    am.updateAppointment(id, newDate, newReason);
                } else if (appointmentChoice == 3) {
                    int id;
                    cout << "Enter Appointment ID to cancel: ";
                    cin >> id;
                    am.cancelAppointment(id);
                } else if (appointmentChoice == 4) {
                    am.listAppointments();
                } else if (appointmentChoice == 0) {
                    cout << "Returning to main menu...\n";
                } else {
                    cout << "Invalid choice.\n";
                }
            }
        } else if (mainChoice == 3) {
            int recordChoice = -1;
            while (recordChoice != 0) {
                recordMenu();
                cin >> recordChoice;
                cin.ignore();

                if (recordChoice == 1) { // Add New Record
                    int id, age;
                    string name;
                    cout << "Enter Patient ID: ";
                    cin >> id;
                    cin.ignore();
                    cout << "Enter Name: ";
                    getline(cin, name);
                    cout << "Enter Age: ";
                    cin >> age;
                    cin.ignore();
                    rm.addRecord(id, name, age);
                } else if (recordChoice == 2) { // Update Existing Record
                    int id;
                    cout << "Enter Patient ID: ";
                    cin >> id;
                    cin.ignore();
                    string entry;
                    cout << "Enter new record entry (e.g., '2025-05-25: Follow-up for BP'): ";
                    getline(cin, entry);
                    rm.updateRecord(id, entry);
                } else if (recordChoice == 3) { // View All Records
                    int id;
                    cout << "Enter Patient ID: ";
                    cin >> id;
                    rm.viewRecord(id);
                } else if (recordChoice == 0) {
                    cout << "Returning to main menu...\n";
                } else {
                    cout << "Invalid choice.\n";
                }
            }
        } else if (mainChoice == 4) {
            lockMonitor.displayLockStatus();
        } else if (mainChoice == 5) {
            lockMonitor.checkDeadlocks();
        } else if (mainChoice == 0) {
            cout << "Terminating program...\n";
        } else {
            cout << "Invalid choice.\n";
        }
    }
    return 0;
}
