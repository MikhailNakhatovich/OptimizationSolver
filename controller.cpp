#include <QFileDialog>
#include <QFileInfo>
#include <QtSql>
#include <QMessageBox>
#include <float.h>
#include <limits.h>

#include "controller.h"
#include "sqlconnectiondialog.h"
#include "insertsolverdialog.h"

Controller::Controller(QWidget *parent)
    : QWidget(parent)
{
    setupUi(this);

    table->addAction(insertRowAction);
    table->addAction(deleteRowAction);
    insButton->setEnabled(false);
    saveImageButton->setEnabled(false);
    drawButton->setEnabled(false);
    spinLeftBorder->setMinimum(-DBL_MAX);
    spinLeftBorder->setMaximum(DBL_MAX);
    spinRightBorder->setMinimum(-DBL_MAX);
    spinRightBorder->setMaximum(DBL_MAX);

    _problem_brocker = _solver_brocker = NULL;
    _problem = NULL;
    _solver = NULL;
}

Controller::~Controller()
{
    if (_problem_brocker) _problem_brocker->release();
    if (_solver_brocker) _solver_brocker->release();
}

void Controller::on_browseButton_clicked()
{
    QString fileName = QFileDialog::getOpenFileName(this, "Select file with problem", "", "DLL (*.dll)");
    editProblem->setText(fileName);
}

QSqlError Controller::changeConnection(const QString &driver, const QString &dbName, const QString &host,
                                       const QString &user, const QString &passwd, int port)
{
    static int cCount = 0;

    QSqlError err;
    QSqlDatabase db = QSqlDatabase::addDatabase(driver, QString("OptimizationSolver%1").arg(++cCount));
    db.setDatabaseName(dbName);
    db.setHostName(host);
    db.setPort(port);
    if (!db.open(user, passwd)) {
        err = db.lastError();
        QSqlDatabase::removeDatabase(QString("OptimizationSolver%1").arg(cCount));
        emit statusMessage(tr("Error connection."));
    } else if (select()) {
        insButton->setEnabled(true);
    } else {
        insButton->setEnabled(false);
        db.close();
        QSqlDatabase::removeDatabase(QString("OptimizationSolver%1").arg(cCount));
    }

    return err;
}

void Controller::changeConnection()
{
    SqlConnectionDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    QSqlError err = changeConnection(dialog.driverName(), dialog.databaseName(), dialog.hostName(),
                                     dialog.userName(), dialog.password(), dialog.port());
    if (err.type() != QSqlError::NoError)
        QMessageBox::warning(this, tr("Unable to open database"), tr("An error occurred while "
                             "opening the connection: ") + err.text());
}

bool Controller::select()
{
    QStringList connectionNames = QSqlDatabase::connectionNames();
    QSqlDatabase curDB = QSqlDatabase::database(connectionNames.value(connectionNames.count() - 1));
    QStringList tables = curDB.tables();
    if (tables.count() == 0) {
        QSqlQueryModel *m = new QSqlQueryModel(table);
        m->setQuery(QSqlQuery("create table solvers(Name text, Description text, Path text);", curDB));
        if (m->lastError().type() != QSqlError::NoError) {
            emit statusMessage(m->lastError().text());
            delete m;
            return false;
        }
        delete m;
    }
    tables = curDB.tables();
    QSqlTableModel *model = new QSqlTableModel(table, curDB);
    model->setEditStrategy(QSqlTableModel::OnRowChange);
    model->setTable(curDB.driver()->escapeIdentifier(tables.at(0), QSqlDriver::TableName));
    model->select();
    if (model->lastError().type() != QSqlError::NoError) {
        emit statusMessage(model->lastError().text());
        delete model;
        return false;
    }
    if (!checkTable(model->record())) {
        emit statusMessage(tr("Bad table."));
        delete model;
        return false;
    }
    table->setModel(model);
    table->setEditTriggers(QAbstractItemView::DoubleClicked|QAbstractItemView::EditKeyPressed);

    connect(table->selectionModel(), SIGNAL(currentRowChanged(QModelIndex, QModelIndex)),
            this, SLOT(currentChanged()));

    if (tables.count() > 1)
        emit statusMessage(tr("Query OK. Count of tables is more than one. Using first table."));
    else
        emit statusMessage(tr("Query OK."));

    updateActions();
    return true;
}

void Controller::updateActions()
{
    bool enableIns = qobject_cast<QSqlTableModel *>(table->model());
    bool enableDel = enableIns && table->currentIndex().isValid();

    insertRowAction->setEnabled(enableIns);
    deleteRowAction->setEnabled(enableDel);
}

void Controller::on_insertRowAction_triggered()
{
    QSqlTableModel *model = qobject_cast<QSqlTableModel *>(table->model());
    if (!model)
        return;

    QModelIndex insertIndex = table->currentIndex();
    int row = insertIndex.row() == -1 ? 0 : insertIndex.row();
    model->insertRow(row);
    insertIndex = model->index(row, 0);
    table->setCurrentIndex(insertIndex);
    table->edit(insertIndex);
}

void Controller::on_deleteRowAction_triggered()
{
    QSqlTableModel *model = qobject_cast<QSqlTableModel *>(table->model());
    if (!model)
        return;

    model->setEditStrategy(QSqlTableModel::OnManualSubmit);

    QModelIndexList currentSelection = table->selectionModel()->selectedIndexes();
    for (int i = 0; i < currentSelection.count(); ++i) {
        if (currentSelection.at(i).column() != 0)
            continue;
        model->removeRow(currentSelection.at(i).row());
    }

    model->submitAll();
    model->setEditStrategy(QSqlTableModel::OnRowChange);

    updateActions();
}

void Controller::on_insButton_clicked()
{
    InsertSolverDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    QStringList connectionNames = QSqlDatabase::connectionNames();
    QSqlDatabase curDB = QSqlDatabase::database(connectionNames.value(connectionNames.count() - 1));
    QStringList tables = curDB.tables();

    QSqlQueryModel *model = new QSqlQueryModel(table);
    QString query = QString("insert into %1 (Name, Description, Path) values ('%2', '%3', '%4');").arg(tables.at(0)).arg(dialog.methodName()).arg(dialog.methodDescription()).arg(dialog.methodPath());
    model->setQuery(QSqlQuery(query, curDB));
    if (model->lastError().type() != QSqlError::NoError) {
        emit statusMessage(model->lastError().text());
    } else {
        select();
    }
    delete model;
}

void Controller::on_solveButton_clicked()
{
    QFileInfo problemInfo(editProblem->text());
    if (!problemInfo.exists()) {
        emit statusMessage(tr("File with problem doesn't exist."));
        return;
    }
    QItemSelectionModel *select = table->selectionModel();
    if (!select || select->selectedRows().count() != 1) {
        emit statusMessage(tr("Solver aren't selected."));
        return;
    }
    QModelIndex index = select->selectedRows(2).at(0);
    QFileInfo solverInfo(index.data().toString());
    if (!solverInfo.exists()) {
        emit statusMessage(tr("File with solver doesn't exist."));
        return;
    }

    QLibrary libProblem(problemInfo.absoluteFilePath(), NULL);
    if (!libProblem.load()) {
        QMessageBox::critical(this, tr("Library with problem failed to load:"), libProblem.errorString());
        return;
    }

    QLibrary libSolver(solverInfo.absoluteFilePath(), NULL);
    if (!libSolver.load()) {
        QMessageBox::critical(this, tr("Library with solver failed to load:"), libSolver.errorString());
        return;
    }

    get_brocker_func problem_brocker_func = NULL, solver_brocker_func = NULL;
    problem_brocker_func = reinterpret_cast<get_brocker_func>(libProblem.resolve("getBrocker"));
    if (!problem_brocker_func) {
        QMessageBox::critical(this, tr("getBrocker wasn't resolved"),
                              tr("Failed to get function getBrocker from library with problem."));
        return;
    }
    solver_brocker_func = reinterpret_cast<get_brocker_func>(libSolver.resolve("getBrocker"));
    if (!solver_brocker_func) {
        QMessageBox::critical(this, tr("getBrocker wasn't resolved"),
                              tr("Failed to get function getBrocker from library with solver."));
        return;
    }

    IBrocker * problem_brocker = NULL, * solver_brocker = NULL;
    IProblem * problem = NULL;
    ISolver * solver = NULL;

    problem_brocker = reinterpret_cast<IBrocker*>(problem_brocker_func());
    if (!problem_brocker) {
        QMessageBox::critical(this, tr("Failed to get brocker"),
                              tr("Failed to get brocker from library with problem."));
        return;
    } else {
        if (problem_brocker->canCastTo(IBrocker::PROBLEM)) {
            problem = reinterpret_cast<IProblem*>(problem_brocker->getInterfaceImpl(IBrocker::PROBLEM));
        } else {
            problem_brocker->release();
            QMessageBox::critical(this, tr("Failed to cast brocker"),
                                  tr("Failed to cast brocker to problem."));
            return;
        }
    }
    solver_brocker = reinterpret_cast<IBrocker*>(solver_brocker_func());
    if (!solver_brocker) {
        problem_brocker->release();
        QMessageBox::critical(this, tr("Failed to get brocker"),
                              tr("Failed to get brocker from library with solver."));
        return;
    } else {
        if (solver_brocker->canCastTo(IBrocker::SOLVER)) {
            solver = reinterpret_cast<ISolver*>(solver_brocker->getInterfaceImpl(IBrocker::SOLVER));
        } else {
            problem_brocker->release();
            solver_brocker->release();
            QMessageBox::critical(this, tr("Failed to cast brocker"),
                                  tr("Failed to cast brocker to solver."));
            return;
        }
    }

    if (solver->setProblem(problem) != ERR_OK) {
        problem_brocker->release();
        solver_brocker->release();
        QMessageBox::critical(this, tr("Failed to set problem"),
                              tr("Failed to set problem to solver."));
        return;
    }
    QUrl url;
    if (solver->getQml(url) != ERR_OK) {
        problem_brocker->release();
        solver_brocker->release();
        QMessageBox::critical(this, tr("Failed to get dialog"),
                              tr("Failed to get dialog from solver for setting parameters."));
        return;
    }
    QString s("parameters"); // open dialog and get parameters form user
    if (solver->setParams(s) != ERR_OK) {
        problem_brocker->release();
        solver_brocker->release();
        QMessageBox::critical(this, tr("Failed to set parameters"),
                              tr("Failed to set parameters to solver."));
        return;
    }
    if (solver->solve() != ERR_OK) {
        problem_brocker->release();
        solver_brocker->release();
        QMessageBox::critical(this, tr("Failed to solve problem"),
                              tr("Solver couldn't solve the problem. See log."));
        return;
    }
    unsigned int dim;
    if (problem->getArgsDim(dim) != ERR_OK) {
        QMessageBox::critical(this, tr("Failed to get dimension"),
                              tr("Failed to get dimension from problem. Plot cannot be drawn."));
    } else {
        spinAxis->setMinimum(1);
        spinAxis->setMaximum(dim);
        drawButton->setEnabled(true);

        if (_problem_brocker) _problem_brocker->release();
        if (_solver_brocker) _solver_brocker->release();
        _problem_brocker = problem_brocker;
        _solver_brocker = solver_brocker;
        _solver = solver;
        _problem = problem;
    }
    emit statusMessage(tr("Problem was solved."));
}

// zaglushka
double f(QVector<double> & x)
{
    return x[0] * x[0] + x[1] * x[1] + x[2] * x[2];
}

void Controller::on_drawButton_clicked()
{
    int axis = spinAxis->text().toInt();
    double a = spinLeftBorder->text().toDouble();
    double b = spinRightBorder->text().toDouble();
    double h = 0.01, ah = (b - a) / (INT_MAX - 2);
    int N = (b - a) / h + 2, i = 0;
    unsigned int dim = spinAxis->maximum();

    if (axis == 0) {
        emit statusMessage(tr("Not supported axis."));
        return;
    }
    if (a > b) {
        emit statusMessage(tr("Bad borders."));
        return;
    }

    if (ah > h) {
        N = INT_MAX;
        h = ah;
    }

    QVector<double> x(N), y(N);
    QVector<double> px(1), py(1);
    double * vector = new (std::nothrow) double[dim];
    if (!vector) {
        emit statusMessage(tr("Failed to alloc memory."));
        return;
    }
    IVector * solution = IVector::createVector(dim, vector);
    delete[] vector;
    if (!solution) {
        emit statusMessage(tr("Failed to alloc memory for solution."));
        return;
    }
    if (_solver->getSolution(solution) != ERR_OK) {
        delete solution;
        emit statusMessage(tr("Failed to get solution from solver."));
        return;
    }
    IVector * iter = solution->clone();
    if (!iter) {
        delete solution;
        emit statusMessage(tr("Failed to alloc memory."));
        return;
    }

    for (double X = a; X <= b; X += h)
    {
        iter->setCoord(axis - 1, X);
        x[i] = X;
        if (!_problem->goalFunction(iter, NULL, y[i]) != ERR_OK) {
            delete solution;
            delete iter;
            emit statusMessage(tr("Failed to get value of goal function."));
            return;
        }
        i++;
    }
    delete iter;
    if (solution->getCoord(axis - 1, px[0]) != ERR_OK) {
        delete solution;
        emit statusMessage(tr("Failed to get value of solution point."));
        return;
    }
    if (!_problem->goalFunction(solution, NULL, py[0]) != ERR_OK) {
        delete solution;
        emit statusMessage(tr("Failed to get value of goal function in the solution point."));
        return;
    }
    delete solution;

    plot->clearGraphs();

    plot->addGraph();
    plot->graph(0)->setData(x, y);

    plot->xAxis->setLabel("x");
    plot->yAxis->setLabel("f");
    plot->xAxis->setRange(a, b);

    double minY = y[0], maxY = y[0];
    for (int i = 1; i < N; ++i)
    {
        if (y[i] < minY) minY = y[i];
        else if (y[i] > maxY) maxY = y[i];
    }
    plot->yAxis->setRange(minY, maxY);

    plot->addGraph(plot->xAxis, plot->yAxis);
    plot->graph(1)->setPen(QColor(50, 50, 50, 255));
    plot->graph(1)->setLineStyle(QCPGraph::lsNone);
    plot->graph(1)->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, 4));
    plot->graph(1)->setData(px, py);

    plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectPlottables);

    plot->replot();
    saveImageButton->setEnabled(true);
}

void Controller::on_saveImageButton_clicked()
{
    QString fileName = QFileDialog::getSaveFileName(this, "Select file with problem", "", "PNG (*.png)");
    QFile file(fileName);

    if (!file.open(QIODevice::WriteOnly))
    {
        emit statusMessage(file.errorString());
    } else {
        plot->savePng(fileName);
        emit statusMessage(tr("Image saved in %1.").arg(fileName));
    }
}

bool Controller::checkTable(QSqlRecord record)
{
    static QString fields[3] = {"Name", "Description", "Path"};
    if (record.count() != 3) return false;
    for (int i = 0; i < 3; ++i)
        if (record.field(i).type() != QVariant::String
                || record.field(i).name().compare(fields[i])) return false;
    return true;
}
