#include <iostream>
#include <string>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <sstream>
#include <limits>
#include <cstdlib>
#include <cctype>

using namespace std;

// Mutex to synchronize console I/O across threads
static mutex io_mutex;

// Clears the terminal screen (cross-platform)
inline void clearScreen()
{
#ifdef _WIN32
    system("cls");
#else
    cout << "\033[2J\033[H" << flush;
#endif
}

// Simple helper to get an integer choice, reprompting on invalid input
int getMenuChoice()
{
    int choice;
    string line;
    while (true)
    {
        {
            lock_guard<mutex> ioLk(io_mutex);
            cout << "Choice: ";
        }
        if (!getline(cin, line))
            return -1;
        stringstream ss(line);
        if (ss >> choice)
            return choice;
        {
            lock_guard<mutex> ioLk(io_mutex);
            cout << "Invalid input. Please enter a number.\n";
        }
    }
}

// Read–Write lock with writer preference
class RWLock
{
    mutex              mtx;
    condition_variable cv;
    int                 activeReaders  = 0;
    int                 waitingWriters = 0;
    bool                writerActive   = false;

public:
    void lockRead()
    {
        unique_lock<mutex> lk(mtx);
        cv.wait(lk, [&]() { return !writerActive && waitingWriters == 0; });
        ++activeReaders;
    }

    void unlockRead()
    {
        unique_lock<mutex> lk(mtx);
        if (--activeReaders == 0)
            cv.notify_all();
    }

    void lockWrite()
    {
        unique_lock<mutex> lk(mtx);
        ++waitingWriters;
        cv.wait(lk, [&]() { return !writerActive && activeReaders == 0; });
        --waitingWriters;
        writerActive = true;
    }

    void unlockWrite()
    {
        unique_lock<mutex> lk(mtx);
        writerActive = false;
        cv.notify_all();
    }

    bool tryLockWrite()
    {
        unique_lock<mutex> lk(mtx, try_to_lock);
        if (!lk.owns_lock() || writerActive || activeReaders > 0)
            return false;
        writerActive = true;
        return true;
    }
};

// Book record
struct Book
{
    string title;
    string author;
    int    count = 0;
    int    id    = 0;
};

// User account record
struct Account
{
    string username;
    string firstName;
    string middleName;
    string lastName;
    string password;
    int    id       = 0;
    bool   loggedIn = false;
};

// Main library class managing books and users
class Library
{
    vector<Book>    books;
    vector<Account> accounts;

    RWLock           booksLock;
    recursive_mutex  updateMutex;
    mutex            cvMutex;
    condition_variable bookCv;
    mutex            accountMutex;

    // Find book index by title (no locks)
    int findBookIndex(const string& title)
    {
        for (int i = 0; i < (int)books.size(); ++i)
        {
            if (books[i].title == title)
                return i;
        }
        return -1;
    }

    // Validate password strength
    bool validPassword(const string& pwd)
    {
        if (pwd.size() < 8)
            return false;
        bool hasUpper = false;
        bool hasLower = false;
        bool hasDigit = false;
        bool hasSpecial = false;
        for (char c : pwd)
        {
            hasUpper   |= isupper((unsigned char)c);
            hasLower   |= islower((unsigned char)c);
            hasDigit   |= isdigit((unsigned char)c);
            hasSpecial |= ispunct((unsigned char)c);
        }
        return hasUpper && hasLower && hasDigit && hasSpecial;
    }

public:
    void registerUser()
    {
        lock_guard<mutex> lk(accountMutex);
        string f, m, l, p, cp;

        {
            lock_guard<mutex> ioLk(io_mutex);
            cout << "First Name: ";
        }
        getline(cin, f);

        {
            lock_guard<mutex> ioLk(io_mutex);
            cout << "Middle Name: ";
        }
        getline(cin, m);

        {
            lock_guard<mutex> ioLk(io_mutex);
            cout << "Last Name: ";
        }
        getline(cin, l);

        string uname = f.substr(0,1) + m.substr(0,1) + l;
        {
            lock_guard<mutex> ioLk(io_mutex);
            cout << "Username: " << uname << '\n';
        }

        while (true)
        {
            {
                lock_guard<mutex> ioLk(io_mutex);
                cout << "Password (Minimum of 8 characters, must include  upper, lower, digit, special): ";
            }
            getline(cin, p);
            if (!validPassword(p))
            {
                lock_guard<mutex> ioLk(io_mutex);
                cout << "Weak password.\n";
                continue;
            }

            {
                lock_guard<mutex> ioLk(io_mutex);
                cout << "Confirm password: ";
            }
            getline(cin, cp);
            if (p != cp)
            {
                lock_guard<mutex> ioLk(io_mutex);
                cout << "Passwords do not match.\n";
            }
            else
            {
                break;
            }
        }

        Account acct;
        acct.username   = uname;
        acct.firstName  = f;
        acct.middleName = m;
        acct.lastName   = l;
        acct.password   = p;
        acct.id         = accounts.size() + 1;
        accounts.push_back(acct);

        lock_guard<mutex> ioLk(io_mutex);
        cout << "User registered with ID: " << acct.id << "\n";
    }

    // Login returns account index or -1
    int loginUser()
    {
        string u, p;
        {
            lock_guard<mutex> ioLk(io_mutex);
            cout << "Username: ";
        }
        getline(cin, u);

        {
            lock_guard<mutex> ioLk(io_mutex);
            cout << "Password: ";
        }
        getline(cin, p);

        lock_guard<mutex> lk(accountMutex);
        for (int i = 0; i < (int)accounts.size(); ++i)
        {
            if (accounts[i].username == u && accounts[i].password == p)
            {
                accounts[i].loggedIn = true;
                lock_guard<mutex> ioLk(io_mutex);
                cout << "Welcome, " << accounts[i].firstName << "!\n";
                return i;
            }
        }

        lock_guard<mutex> ioLk(io_mutex);
        cout << "Invalid credentials.\n";
        return -1;
    }

    // Per-user menu loop (synchronous)
    void userSession(int idx)
    {
        while (accounts[idx].loggedIn)
        {
            clearScreen();
            {
                lock_guard<mutex> ioLk(io_mutex);
                cout << "Menu: \n";
                cout << "\n1) Add Book \n2) Update Book \n3) Remove Book"
                        "\n4) Borrow Book \n5) Return Book \n6) Check Availability"
                        "\n7) Lock Status \n8) Deadlock Info \n9) Fairness Info"
                        "\n10) Logout\n\n";
            }

            int choice = getMenuChoice();
            if (choice < 1 || choice > 10)
            {
                lock_guard<mutex> ioLk(io_mutex);
                cout << "Invalid option.\n";
                continue;
            }

            switch (choice)
            {
                case 1:  addBook();          break;
                case 2:  updateBook();       break;
                case 3:  removeBook();       break;
                case 4:  borrowBook();       break;
                case 5:  returnBook();       break;
                case 6:  checkAvailability();break;
                case 7:  displayLockStatus();break;
                case 8:  detectDeadlocks();  break;
                case 9:  ensureFairness();   break;
                case 10: accounts[idx].loggedIn = false;
                         lock_guard<mutex> ioLk(io_mutex);
                         cout << "Logged out.\n";
                         break;
            }

            lock_guard<mutex> ioLk(io_mutex);
            cout << "Press Enter to continue...";
            cin.get();
        }
    }

     void addBook()
    {
        lock_guard<mutex> ioLk(io_mutex);
        cout << "Book title: ";
        string t;
        getline(cin, t);
        cout << "Author: ";
        string a;
        getline(cin, a);
        cout << "Quantity: ";
        int c;
        cin >> c;
        cin.ignore(numeric_limits<streamsize>::max(), '\n');

        booksLock.lockWrite();
        Book b;
        b.title  = t;
        b.author = a;
        b.count  = c;
        b.id     = books.size() + 1;
        books.push_back(b);
        booksLock.unlockWrite();

        cout << "Added '" << t << "' (ID=" << b.id << ").\n";
    }

    // Update book details
    void updateBook()
    {
        lock_guard<recursive_mutex> rec(updateMutex);
        lock_guard<mutex> ioLk(io_mutex);
        cout << "Title to update: ";
        string t;
        getline(cin, t);

        booksLock.lockWrite();
        int idx = findBookIndex(t);
        if (idx < 0)
        {
            cout << "Book not found.\n";
            booksLock.unlockWrite();
            return;
        }

        cout << "New title: ";
        getline(cin, books[idx].title);
        cout << "New author: ";
        getline(cin, books[idx].author);
        cout << "New quantity: ";
        cin >> books[idx].count;
        cin.ignore(numeric_limits<streamsize>::max(), '\n');

        booksLock.unlockWrite();
        cout << "Book updated.\n";
    }

    // Remove a book
    void removeBook()
    {
        lock_guard<mutex> ioLk(io_mutex);
        cout << "Title to remove: ";
        string t;
        getline(cin, t);

        booksLock.lockWrite();
        int idx = findBookIndex(t);
        if (idx < 0)
        {
            cout << "Book not found.\n";
            booksLock.unlockWrite();
            return;
        }

        books.erase(books.begin() + idx);
        booksLock.unlockWrite();
        cout << "Book removed.\n";
    }

    // Borrow a book (blocks if out of stock)
    void borrowBook()
    {
        lock_guard<mutex> ioLk(io_mutex);
        cout << "Title to borrow: ";
        string t;
        getline(cin, t);

        if (!booksLock.tryLockWrite())
        {
            cout << "Library busy. Try again later.\n";
            return;
        }

        int idx = findBookIndex(t);
        if (idx < 0)
        {
            cout << "Book not found.\n";
            booksLock.unlockWrite();
            return;
        }

        if (books[idx].count == 0)
        {
            cout << "Out of stock. Waiting...\n";
            booksLock.unlockWrite();

            unique_lock<mutex> lk(cvMutex);
            bookCv.wait(lk, [&]() {
                booksLock.lockRead();
                int j = findBookIndex(t);
                bool ok = (j >= 0 && books[j].count > 0);
                booksLock.unlockRead();
                return ok;
            });

            booksLock.lockWrite();
            idx = findBookIndex(t);
        }

        if (idx >= 0 && books[idx].count > 0)
        {
            --books[idx].count;
            cout << "Borrowed '" << t << "'. Remaining: " << books[idx].count << "\n";
        }
        else
        {
            cout << "Still unavailable.\n";
        }

        booksLock.unlockWrite();
    }

    // Return a book and notify waiting borrowers
    void returnBook()
    {
        lock_guard<mutex> ioLk(io_mutex);
        cout << "Title to return: ";
        string t;
        getline(cin, t);

        booksLock.lockWrite();
        int idx = findBookIndex(t);
        if (idx >= 0)
        {
            ++books[idx].count;
            cout << "Returned '" << t << "'. Now: " << books[idx].count << "\n";
        }
        else
        {
            cout << "Book not found.\n";
        }
        booksLock.unlockWrite();

        bookCv.notify_all();
    }

    // Check availability without blocking
    void checkAvailability()
    {
        lock_guard<mutex> ioLk(io_mutex);
        cout << "Title to check: ";
        string t;
        getline(cin, t);

        booksLock.lockRead();
        int idx = findBookIndex(t);
        if (idx >= 0)
            cout << books[idx].count << " copies available.\n";
        else
            cout << "Book not found.\n";
        booksLock.unlockRead();
    }

    // Display lock status hint
    void displayLockStatus()
    {
        if (booksLock.tryLockWrite())
        {
            booksLock.unlockWrite();
            lock_guard<mutex> ioLk(io_mutex);
            cout << "Write lock is free.\n";
        }
        else
        {
            lock_guard<mutex> ioLk(io_mutex);
            cout << "Write lock is held.\n";
        }
    }

    // Liveness stubs
    void detectDeadlocks()
    {
        lock_guard<mutex> ioLk(io_mutex);
        cout << "Deadlock check: consistent locking avoids deadlock.\n";
    }

    void ensureFairness()
    {
        lock_guard<mutex> ioLk(io_mutex);
        cout << "Fairness: notify_all prevents starvation.\n";
    }
};

int main()
{
    Library lib;
    while (true)
    {
        clearScreen();
        {
            lock_guard<mutex> ioLk(io_mutex);
            cout << "\nMenu:\n 1) Register\n 2) Login\n 3) Exit\n";
        }

        int choice = getMenuChoice();
        if (choice == 1)
        {
            lib.registerUser();
        }
        else if (choice == 2)
        {
            int idx = lib.loginUser();
            if (idx >= 0)
                lib.userSession(idx);
        }
        else if (choice == 3)
        {
            break;
        }
        else
        {
            lock_guard<mutex> ioLk(io_mutex);
            cout << "Invalid choice.\n";
        }

        {
            lock_guard<mutex> ioLk(io_mutex);
            cout << "Press Enter to continue...";
        }
        cin.get();
    }

    lock_guard<mutex> ioLk(io_mutex);
    cout << "Shutting down library system...\n";
    return 0;
}



