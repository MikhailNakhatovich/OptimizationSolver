#include <new>
#include <QFile>
#include <cmath>

#include "ILog.h"
#include "IBrocker.h"
#include "ISolver.h"
#include "IProblem.h"
#include "ICompact.h"

namespace {

class Solver1 : public ISolver {

public:

   int getId() const;

   int setParams(IVector const* params);
   int setParams(QString& str);
   int setProblem(IProblem *ptr);
   int solve();
   int getSolution(IVector* &vec) const;
   int getQml(QUrl& qml) const;

     /*ctor*/
   Solver1();

     /*dtor*/
   ~Solver1();

private:

   IVector * _args, * _params;
   ICompact * _compact;
   bool solveByArgs;
   double eps;
   IVector * _prev, * _next;
   IProblem * _problem;

};

class Brocker2 : public IBrocker {

public:

   int getId() const;

   bool canCastTo(Type type) const;
   void* getInterfaceImpl(Type type) const;

   int release();

     /*ctor*/
   Brocker2(Solver1 *solver);

     /*dtor*/
   ~Brocker2();

private:

    Solver1 *_solver;

};

}


Solver1::Solver1():
    _args(NULL), _params(NULL), _prev(NULL), _next(NULL), _compact(NULL)
 {}

Solver1::~Solver1() {
    delete _args;
    delete _params;
    delete _prev;
    delete _next;
    delete _compact;
}

int Solver1::getId() const {
    return ISolver::INTERFACE_0;
}

int Solver1::setProblem(IProblem * ptr) {
    if (!ptr) {
        ILog::report("ISolver.setProblem: Input parameter ptr is nullptr\n");
        return ERR_WRONG_PROBLEM;
    }
    unsigned dim;
    if (_args) {
        if (ptr->getArgsDim(dim) != ERR_OK) {
            ILog::report("ISolver.setProblem: Cannot check dimensions of arguments\n");
            return ERR_ANY_OTHER;
        }
        if (_args->getDim() != dim) {
            ILog::report("ISolver.setProblem: Dimensions of arguments mismatch\n");
            return ERR_WRONG_PROBLEM;
        }
    }
    if (_params) {
        if (ptr->getParamsDim(dim) != ERR_OK) {
            ILog::report("ISolver.setProblem: Cannot check dimensions of parameters\n");
            return ERR_ANY_OTHER;
        }
        if (_params->getDim() != dim) {
            ILog::report("ISolver.setProblem: Dimensions of parameters mismatch\n");
            return ERR_WRONG_PROBLEM;
        }
    }
    _problem = ptr;
    return ERR_OK;
}

int Brocker2::getId() const {
    return IBrocker::INTERFACE_0;
}

int Solver1::getQml(QUrl& qml) const {
    QString file = "qrc:/solver1.qml";
    if (!QFile::exists(file))
        return ERR_ANY_OTHER;
    qml = QUrl::fromLocalFile(file);
    return ERR_OK;
}

bool Brocker2::canCastTo(Type type) const {
    switch (type) {
    case IBrocker::SOLVER:
        return true;
    default:
        return false;
    }
}

void* Brocker2::getInterfaceImpl(Type type) const {
    switch (type) {
    case IBrocker::SOLVER:
        return _solver;
    default:
        return NULL;
    }
}

int Brocker2::release() {
    delete this;

    return ERR_OK;
}

Brocker2::Brocker2(Solver1 *solver):
    _solver(solver)
{}

Brocker2::~Brocker2() {
    delete _solver;
}

extern "C" {
SHARED_EXPORT void* getBrocker() {
    Solver1 *solver = new (std::nothrow) Solver1();

    if (!solver) {
        ILog::report("getBrocker: not enough memory\n");
        return NULL;
    }

    Brocker2 *brocker = new (std::nothrow) Brocker2(solver);

    if (!brocker) {
        ILog::report("getBrocker: not enough memory\n");
        delete solver;
        return NULL;
    }

    return brocker;
}
}
