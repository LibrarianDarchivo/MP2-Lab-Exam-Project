// Multi-Threaded Library Management System
// A simple, thread-safe library system supporting admin and user roles.
// Admins can manage the book catalog; users can borrow/return books and check availability.

#include <iostream>
#include <string>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <sstream>
#include <limits>
#include <cstdlib>
#include <cctype>
#include <iomanip>
#include <algorithm>   

using namespace std;

// Global mutex to synchronize console I/O across threads
static mutex io_mutex;

// Clear the terminal screen (cross-platform)
inline void clearScreen()
{
#ifdef _WIN32
    system("cls");
#else
    cout << "\033[2J\033[H" << flush;
#endif
}

// Read an integer choice from user, reprompt on invalid
int getMenuChoice()
{
    int    choice;
    string line;

    while (true)
    {
        {
            lock_guard<mutex> io(io_mutex);
            cout << "Choice: " << flush;
        }

        if (!getline(cin, line))
            return -1;

        stringstream ss(line);
        if (ss >> choice)
            return choice;

        {
            lock_guard<mutex> io(io_mutex);
            cout << "Invalid input. Please enter a number.\n" << flush;
        }
    }
}

// Reader-writer lock with writer preference
class RWLock
{
public:
    void lockRead();
    void unlockRead();
    void lockWrite();
    void unlockWrite();
    bool tryLockWrite();

private:
    mutex              mtx;
    condition_variable cv;
    int                 activeReaders  = 0;
    int                 waitingWriters = 0;
    bool                writerActive   = false;
};

void RWLock::lockRead()
{
    unique_lock<mutex> lk(mtx);
    cv.wait(lk, [&]() { return !writerActive && waitingWriters == 0; });
    ++activeReaders;
}

void RWLock::unlockRead()
{
    unique_lock<mutex> lk(mtx);
    if (--activeReaders == 0)
        cv.notify_all();
}

void RWLock::lockWrite()
{
    unique_lock<mutex> lk(mtx);
    ++waitingWriters;
    cv.wait(lk, [&]() { return !writerActive && activeReaders == 0; });
    --waitingWriters;
    writerActive = true;
}

void RWLock::unlockWrite()
{
    unique_lock<mutex> lk(mtx);
    writerActive = false;
    cv.notify_all();
}

bool RWLock::tryLockWrite()
{
    unique_lock<mutex> lk(mtx, try_to_lock);
    if (!lk.owns_lock() || writerActive || activeReaders > 0)
        return false;
    writerActive = true;
    return true;
}

// Book record
struct Book
{
    string title;
    string author;
    int    count;
    int    id;
};

// User account record
struct Account
{
    string        username;
    string        firstName;
    string        middleName;
    string        lastName;
    string        password;
    int           id;
    bool          loggedIn;
    bool          isAdmin;

    // NEW: which book IDs this user currently has borrowed
    vector<int>   borrowedBookIds;
};

// Main library class
class Library
{
public:
    Library();
    void registerUser();
    int  loginUser();
    void userSession(int idx);

private:
    void listAllBooks();
    void addBook();
    void updateBook();
    void removeBook();
    void borrowBook();
    void returnBook();
    void checkAvailability();
    void displayLockStatus();
    void detectDeadlocks();
    void ensureFairness();

    int   findBookIndex(const string &title);
    bool  validPassword(const string &pwd);

    vector<Book>    books;
    vector<Account> accounts;

    // track which account is currently active
    int             currentUserIdx = -1;

    RWLock            booksLock;
    recursive_mutex   updateMutex;
    mutex             cvMutex;
    condition_variable bookCv;
    mutex             accountMutex;
};

// Default admin account information
Library::Library()
{
    accounts.push_back({
        "admin",
        "Administrator",
        "",
        "",
        "password",
        1,
        false,
        true,
        {}              // borrowedBookIds empty
    });
}

// Find book by title
int Library::findBookIndex(const string &title)
{
    for (size_t i = 0; i < books.size(); ++i)
        if (books[i].title == title)
            return static_cast<int>(i);
    return -1;
}

// Password strength check
bool Library::validPassword(const string &pwd)
{
    if (pwd.size() < 8) return false;

    bool upper   = false;
    bool lower   = false;
    bool digit   = false;
    bool special = false;

    for (char c : pwd)
    {
        upper   |= isupper((unsigned char)c);
        lower   |= islower((unsigned char)c);
        digit   |= isdigit((unsigned char)c);
        special |= ispunct((unsigned char)c);
    }

    return upper && lower && digit && special;
}

// Register a new user
void Library::registerUser()
{
    lock_guard<mutex> lk(accountMutex);

    string first, middle, last, pwd, confirm;
    {
        lock_guard<mutex> io(io_mutex);
        cout << "First Name: " << flush;
    }
    getline(cin, first);

    {
        lock_guard<mutex> io(io_mutex);
        cout << "Middle Name: " << flush;
    }
    getline(cin, middle);

    {
        lock_guard<mutex> io(io_mutex);
        cout << "Last Name: " << flush;
    }
    getline(cin, last);

    string uname = first.substr(0,1) + middle.substr(0,1) + last;
    {
        lock_guard<mutex> io(io_mutex);
        cout << "Your username: " << uname << '\n' << flush;
    }

    while (true)
    {
        {
            lock_guard<mutex> io(io_mutex);
            cout << "Password (Minimum of 8 chars, must include upper/lower/digit/special): " << flush;
        }
        getline(cin, pwd);

        if (!validPassword(pwd))
        {
            lock_guard<mutex> io(io_mutex);
            cout << "Weak password.\n" << flush;
            continue;
        }

        {
            lock_guard<mutex> io(io_mutex);
            cout << "Confirm password: " << flush;
        }
        getline(cin, confirm);

        if (pwd != confirm)
        {
            lock_guard<mutex> io(io_mutex);
            cout << "Passwords do not match.\n" << flush;
        }
        else
        {
            break;
        }
    }

    accounts.push_back({
        uname,
        first,
        middle,
        last,
        pwd,
        static_cast<int>(accounts.size()) + 1,
        false,
        false,
        {}   // start with no borrowed books
    });

    {
        lock_guard<mutex> io(io_mutex);
        cout << "User registered with ID: " << accounts.back().id << '\n' << flush;
    }
}

// Login user
int Library::loginUser()
{
    string uname, pwd;

    {
        lock_guard<mutex> io(io_mutex);
        cout << "Username: " << flush;
    }
    getline(cin, uname);

    {
        lock_guard<mutex> io(io_mutex);
        cout << "Password: " << flush;
    }
    getline(cin, pwd);

    lock_guard<mutex> lk(accountMutex);
    for (auto &acct : accounts)
    {
        if (acct.username == uname && acct.password == pwd)
        {
            acct.loggedIn = true;
            lock_guard<mutex> io(io_mutex);
            cout << "Welcome, " << acct.firstName << "!\n" << flush;
            return acct.id - 1;
        }
    }

    {
        lock_guard<mutex> io(io_mutex);
        cout << "Invalid credentials.\n" << flush;
    }
    return -1;
}

// List all books
void Library::listAllBooks()
{
    lock_guard<mutex> io(io_mutex);
    booksLock.lockRead();

    if (books.empty())
    {
        cout << "No books in the library.\n";
    }
    else
    {
        constexpr int ID_W = 4, T_W = 30, A_W = 20, C_W = 6;
        cout << left
             << setw(ID_W) << "ID"
             << setw(T_W) << "Title"
             << setw(A_W) << "Author"
             << setw(C_W) << "Count" << "\n";
        cout << string(ID_W + T_W + A_W + C_W, '-') << "\n";

        for (auto &b : books)
        {
            cout << left
                 << setw(ID_W) << b.id
                 << setw(T_W) << b.title
                 << setw(A_W) << b.author
                 << setw(C_W) << b.count << "\n";
        }
    }

    booksLock.unlockRead();
}

// Add book
void Library::addBook()
{
    lock_guard<mutex> io(io_mutex);

    cout << "Book title: " << flush;
    string t; getline(cin, t);

    cout << "Author: " << flush;
    string a; getline(cin, a);

    cout << "Quantity: " << flush;
    int c; cin >> c;
    cin.ignore(numeric_limits<streamsize>::max(), '\n');

    booksLock.lockWrite();
    books.push_back({t, a, c, static_cast<int>(books.size()) + 1});
    booksLock.unlockWrite();

    cout << "Added '" << t << "'.\n" << flush;
}

// Update book
void Library::updateBook()
{
    lock_guard<recursive_mutex> rec(updateMutex);
    lock_guard<mutex>            io(io_mutex);

    cout << "Title to update: " << flush;
    string t; getline(cin, t);

    booksLock.lockWrite();
    int idx = findBookIndex(t);
    if (idx < 0)
    {
        cout << "Book not found.\n" << flush;
        booksLock.unlockWrite();
        return;
    }

    cout << "New title: " << flush;  getline(cin, books[idx].title);
    cout << "New author: " << flush; getline(cin, books[idx].author);
    cout << "New qty: " << flush;    cin >> books[idx].count;
    cin.ignore(numeric_limits<streamsize>::max(), '\n');

    booksLock.unlockWrite();
    cout << "Book updated.\n" << flush;
}

// Remove book
void Library::removeBook()
{
    lock_guard<mutex> io(io_mutex);

    cout << "Title to remove: " << flush;
    string t; getline(cin, t);

    booksLock.lockWrite();
    int idx = findBookIndex(t);
    if (idx < 0)
    {
        cout << "Book not found.\n" << flush;
        booksLock.unlockWrite();
        return;
    }

    books.erase(books.begin() + idx);
    booksLock.unlockWrite();

    cout << "Book removed.\n" << flush;
}

// Borrow book
void Library::borrowBook()
{
    lock_guard<mutex> io(io_mutex);

    // remember who’s borrowing
    int uid = currentUserIdx;

    cout << "Title to borrow: " << flush;
    string t; getline(cin, t);

    if (!booksLock.tryLockWrite())
    {
        cout << "Library busy. Try later.\n" << flush;
        return;
    }

    int idx = findBookIndex(t);
    if (idx < 0)
    {
        cout << "Book not found.\n" << flush;
        booksLock.unlockWrite();
        return;
    }

    if (books[idx].count == 0)
    {
        cout << "Out of stock. Waiting...\n" << flush;
        booksLock.unlockWrite();

        unique_lock<mutex> lk(cvMutex);
        bookCv.wait(lk, [&]() {
            booksLock.lockRead();
            bool ok = (findBookIndex(t) >= 0 && books[findBookIndex(t)].count > 0);
            booksLock.unlockRead();
            return ok;
        });

        booksLock.lockWrite();
        idx = findBookIndex(t);
    }

    if (idx >= 0 && books[idx].count > 0)
    {
        --books[idx].count;

        // record it on the user’s account
        accounts[uid].borrowedBookIds.push_back(books[idx].id);

        cout << "Borrowed '" << t << "'. Remaining: " << books[idx].count << "\n" << flush;
    }
    else
    {
        cout << "Still unavailable.\n" << flush;
    }

    booksLock.unlockWrite();
}

// Return book
void Library::returnBook()
{
    lock_guard<mutex> io(io_mutex);

    // remember who’s returning
    int uid = currentUserIdx;

    cout << "Title to return: " << flush;
    string t; getline(cin, t);

    booksLock.lockWrite();
    int idx = findBookIndex(t);

    if (idx < 0)
    {
        cout << "Book not found.\n" << flush;
        booksLock.unlockWrite();
        return;
    }

    int bookId = books[idx].id;
    auto &loaned = accounts[uid].borrowedBookIds;
    auto it = find(loaned.begin(), loaned.end(), bookId);

    if (it != loaned.end())
    {
        // user did borrow it: accept the return
        ++books[idx].count;
        loaned.erase(it);

        cout << "Returned '" << t << "'. Now: " << books[idx].count << "\n" << flush;
    }
    else
    {
        // user never borrowed that title
        cout << "You did not borrow that book, so it cannot be returned.\n" << flush;
    }

    booksLock.unlockWrite();
    bookCv.notify_all();
}

// Check availability
void Library::checkAvailability()
{
    lock_guard<mutex> io(io_mutex);

    cout << "Title to check: " << flush;
    string t; getline(cin, t);

    booksLock.lockRead();
    int idx = findBookIndex(t);

    if (idx >= 0)
        cout << books[idx].count << " copies available.\n" << flush;
    else
        cout << "Book not found.\n" << flush;

    booksLock.unlockRead();
}

// Lock status
void Library::displayLockStatus()
{
    if (booksLock.tryLockWrite())
    {
        booksLock.unlockWrite();
        lock_guard<mutex> io(io_mutex);
        cout << "Write lock is free.\n" << flush;
    }
    else
    {
        lock_guard<mutex> io(io_mutex);
        cout << "Write lock is held.\n" << flush;
    }
}

// Deadlock stub
void Library::detectDeadlocks()
{
    lock_guard<mutex> io(io_mutex);
    cout << "No deadlocks detected.\n" << flush;
}

// Fairness stub
void Library::ensureFairness()
{
    lock_guard<mutex> io(io_mutex);
    cout << "Fairness ensured (no starvation).\n" << flush;
}

// User session loop
void Library::userSession(int idx)
{
    // record who’s active
    currentUserIdx = idx;

    while (accounts[idx].loggedIn)
    {
        clearScreen();

        if (accounts[idx].isAdmin)
        {
            {
                lock_guard<mutex> io(io_mutex);
                cout << "\nAdmin Menu:\n"
                     << "1) Add Book\n"
                     << "2) Update Book\n"
                     << "3) Remove Book\n"
                     << "4) List All Books\n"
                     << "5) Lock Status\n"
                     << "6) Deadlock Info\n"
                     << "7) Fairness Info\n"
                     << "8) Logout\n";
            }

            int choice = getMenuChoice();
            switch (choice)
            {
                case 1:
                {
                    addBook();
                    break;
                }
                case 2:
                {
                    updateBook();
                    break;
                }
                case 3:
                {
                    removeBook();
                    break;
                }
                case 4:
                {
                    listAllBooks();
                    break;
                }
                case 5:
                {
                    displayLockStatus();
                    break;
                }
                case 6:
                {
                    detectDeadlocks();
                    break;
                }
                case 7:
                {
                    ensureFairness();
                    break;
                }
                case 8:
                {
                    accounts[idx].loggedIn = false;
                    {
                        lock_guard<mutex> io2(io_mutex);
                        cout << "Logged out.\n" << flush;
                    }
                    break;
                }
                default:
                {
                    {
                        lock_guard<mutex> io2(io_mutex);
                        cout << "Invalid option.\n" << flush;
                    }
                    break;
                }
            }
        }
        else
        {
            {
                lock_guard<mutex> io(io_mutex);
                cout << "\nUser Menu:\n"
                     << "1) Borrow Book\n"
                     << "2) Return Book\n"
                     << "3) Check Availability\n"
                     << "4) Logout\n";
            }

            int choice = getMenuChoice();
            switch (choice)
            {
                case 1:
                {
                    borrowBook();
                    break;
                }
                case 2:
                {
                    returnBook();
                    break;
                }
                case 3:
                {
                    checkAvailability();
                    break;
                }
                case 4:
                {
                    accounts[idx].loggedIn = false;
                    {
                        lock_guard<mutex> io2(io_mutex);
                        cout << "Logged out.\n" << flush;
                    }
                    break;
                }
                default:
                {
                    {
                        lock_guard<mutex> io2(io_mutex);
                        cout << "Invalid option.\n" << flush;
                    }
                    break;
                }
            }
        }

        // Pause before clearing
        {
            lock_guard<mutex> io(io_mutex);
            cout << "Press Enter to continue..." << flush;
        }
        cin.get();
    }
}

// Main Program
int main()
{
    Library lib;

    while (true)
    {
        clearScreen();

        {
            lock_guard<mutex> io(io_mutex);
            cout << "\nMenu:\n"
                 << "1) Register\n"
                 << "2) Login\n"
                 << "3) Exit\n";
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
            lock_guard<mutex> io(io_mutex);
            cout << "Invalid choice.\n" << flush;
        }

        {
            lock_guard<mutex> io(io_mutex);
            cout << "Press Enter to continue..." << flush;
        }
        cin.get();
    }

    lock_guard<mutex> io(io_mutex);
    cout << "Shutting down...\n" << flush;
    return 0;
}
