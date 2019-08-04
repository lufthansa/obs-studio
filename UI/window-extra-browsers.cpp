#include "window-extra-browsers.hpp"
#include "window-basic-main.hpp"
#include "qt-wrappers.hpp"
#include "window-dock.hpp"

#include <QLineEdit>
#include <QHBoxLayout>

#include "ui_OBSExtraBrowsers.h"

#include <browser-panel.hpp>
extern QCef *cef;
extern QCefCookieManager *panel_cookies;

enum class Column : int {
	Delete,
	Title,
	Url,

	Count,
};

class ExtraBrowser : public OBSDock {
public:
	inline ExtraBrowser() : OBSDock() {}

	QScopedPointer<QCefWidget> widget;

	inline void SetWidget(QCefWidget *widget_)
	{
		setWidget(widget_);
		widget.reset(widget_);
	}
};

/* ------------------------------------------------------------------------- */

void ExtraBrowsersModel::Reset()
{
	items.clear();

	OBSBasic *main = OBSBasic::Get();

	for (int i = 0; i < main->extraBrowserDocks.size(); i++) {
		ExtraBrowser *dock = reinterpret_cast<ExtraBrowser *>(
			main->extraBrowserDocks[i].data());

		Item item;
		item.prevIdx = i;
		item.title = dock->windowTitle();
		item.url = main->extraBrowserDockTargets[i];
		items.push_back(item);
	}
}

int ExtraBrowsersModel::rowCount(const QModelIndex &) const
{
	int count = items.size() + 1;
	return count;
}

int ExtraBrowsersModel::columnCount(const QModelIndex &) const
{
	return (int)Column::Count;
}

QVariant ExtraBrowsersModel::data(const QModelIndex &index, int role) const
{
	int column = index.column();
	int idx = index.row();
	int count = items.size();
	bool validRole = role == Qt::DisplayRole ||
			 role == Qt::AccessibleTextRole;

	if (!validRole)
		return QVariant();

	if (idx >= 0 && idx < count) {
		switch (column) {
		case (int)Column::Title:
			return items[idx].title;
		case (int)Column::Url:
			return items[idx].url;
		}
	} else if (idx == count) {
		switch (column) {
		case (int)Column::Title:
			return newTitle;
		case (int)Column::Url:
			return newURL;
		}
	}

	return QVariant();
}

QVariant ExtraBrowsersModel::headerData(int section,
					Qt::Orientation orientation,
					int role) const
{
	bool validRole = role == Qt::DisplayRole ||
			 role == Qt::AccessibleTextRole;

	if (validRole && orientation == Qt::Orientation::Horizontal) {
		switch (section) {
		case (int)Column::Title:
			return QTStr("Title");
		case (int)Column::Url:
			return QStringLiteral("URL");
		}
	}

	return QVariant();
}

Qt::ItemFlags ExtraBrowsersModel::flags(const QModelIndex &index) const
{
	Qt::ItemFlags flags = QAbstractTableModel::flags(index);

	if (index.column() != (int)Column::Delete)
		flags |= Qt::ItemIsEditable;

	return flags;
}

class DelButton : public QPushButton {
public:
	inline DelButton(QModelIndex index_) : QPushButton(), index(index_) {}

	QPersistentModelIndex index;
};

void ExtraBrowsersModel::AddDeleteButton(int idx)
{
	QTableView *widget = reinterpret_cast<QTableView *>(parent());

	QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Expanding,
			   QSizePolicy::PushButton);
	policy.setWidthForHeight(true);

	QModelIndex index = createIndex(idx, 0, nullptr);

	QPushButton *del = new DelButton(index);
	del->setProperty("themeID", "trashIcon");
	del->setSizePolicy(policy);
	connect(del, &QPushButton::clicked, this,
		&ExtraBrowsersModel::DeleteItem);

	widget->setIndexWidget(index, del);
}

void ExtraBrowsersModel::CheckToAdd()
{
	if (newTitle.isEmpty() || newURL.isEmpty())
		return;

	int idx = items.size() + 1;
	beginInsertRows(QModelIndex(), idx, idx);

	Item item;
	item.prevIdx = -1;
	item.title = newTitle;
	item.url = newURL;
	items.push_back(item);

	newTitle = "";
	newURL = "";

	endInsertRows();

	AddDeleteButton(idx - 1);
}

void ExtraBrowsersModel::UpdateItem(Item &item)
{
	int idx = item.prevIdx;

	OBSBasic *main = OBSBasic::Get();
	ExtraBrowser *dock = reinterpret_cast<ExtraBrowser *>(
		main->extraBrowserDocks[idx].data());
	dock->setWindowTitle(item.title);
	dock->setObjectName(item.title);

	if (main->extraBrowserDockTargets[idx] != item.url) {
		dock->widget->setURL(QT_TO_UTF8(item.url));
		main->extraBrowserDockTargets[idx] = item.url;
	}
}

void ExtraBrowsersModel::DeleteItem()
{
	QTableView *widget = reinterpret_cast<QTableView *>(parent());

	DelButton *del = reinterpret_cast<DelButton *>(sender());
	int row = del->index.row();

	/* there's some sort of internal bug in Qt and deleting certain index
	 * widgets or "editors" that can cause a crash inside Qt if the widget
	 * is not manually removed, at least on 5.7 */
	widget->setIndexWidget(del->index, nullptr);
	del->deleteLater();

	/* --------- */

	beginRemoveRows(QModelIndex(), row, row);

	int prevIdx = items[row].prevIdx;
	items.removeAt(row);

	if (prevIdx != -1) {
		int i = 0;
		for (; i < deleted.size() && deleted[i] < prevIdx; i++)
			;
		deleted.insert(i, prevIdx);
	}

	endRemoveRows();
}

void ExtraBrowsersModel::Apply()
{
	OBSBasic *main = OBSBasic::Get();

	for (Item &item : items) {
		if (item.prevIdx != -1) {
			UpdateItem(item);
		} else {
			main->AddExtraBrowserDock(item.title, item.url);
		}
	}

	for (int i = deleted.size() - 1; i >= 0; i--) {
		int idx = deleted[i];
		main->extraBrowserDocks.removeAt(idx);
		main->extraBrowserDockActions.removeAt(idx);
		main->extraBrowserDockTargets.removeAt(idx);
	}

	deleted.clear();

	Reset();
}

/* ------------------------------------------------------------------------- */

QWidget *ExtraBrowsersDelegate::createEditor(QWidget *parent,
					     const QStyleOptionViewItem &,
					     const QModelIndex &index) const
{
	QLineEdit *text = new QLineEdit(parent);
	text->setProperty("row", index.row());
	text->setProperty("col", index.column());
	text->installEventFilter(const_cast<ExtraBrowsersDelegate *>(this));
	text->setSizePolicy(QSizePolicy(QSizePolicy::Policy::Expanding,
					QSizePolicy::Policy::Expanding,
					QSizePolicy::ControlType::LineEdit));
	return text;
}

void ExtraBrowsersDelegate::setEditorData(QWidget *editor,
					  const QModelIndex &index) const
{
	QLineEdit *text = reinterpret_cast<QLineEdit *>(editor);
	text->blockSignals(true);
	text->setText(index.data().toString());
	text->blockSignals(false);
}

bool ExtraBrowsersDelegate::eventFilter(QObject *object, QEvent *event)
{
	QLineEdit *edit = qobject_cast<QLineEdit *>(object);
	if (!edit)
		return false;

	if (LineEditChanged(event)) {
		UpdateText(edit);
		return true;
	}

	return false;
}

void ExtraBrowsersDelegate::UpdateText(QLineEdit *edit)
{
	int row = edit->property("row").toInt();
	int col = edit->property("col").toInt();

	QString text = edit->text();

	if (row < model->items.size()) {
		/* if edited existing item, update it*/
		switch (col) {
		case (int)Column::Title:
			model->items[row].title = text;
			break;
		case (int)Column::Url:
			model->items[row].url = text;
			break;
		}
	} else {
		/* if both new values filled out, create new one */
		switch (col) {
		case (int)Column::Title:
			model->newTitle = text;
			break;
		case (int)Column::Url:
			model->newURL = text;
			break;
		}

		model->CheckToAdd();
	}

	emit commitData(edit);
}

/* ------------------------------------------------------------------------- */

OBSExtraBrowsers::OBSExtraBrowsers()
	: QWidget(nullptr), ui(new Ui::OBSExtraBrowsers)
{
	ui->setupUi(this);

	setAttribute(Qt::WA_DeleteOnClose, true);

	model = new ExtraBrowsersModel(ui->table);

	ui->table->setModel(model);
	ui->table->setItemDelegateForColumn((int)Column::Title,
					    new ExtraBrowsersDelegate(model));
	ui->table->setItemDelegateForColumn((int)Column::Url,
					    new ExtraBrowsersDelegate(model));
	ui->table->horizontalHeader()->setSectionResizeMode(
		QHeaderView::ResizeMode::Stretch);
	ui->table->horizontalHeader()->setSectionResizeMode(
		(int)Column::Delete, QHeaderView::ResizeMode::Fixed);
	ui->table->setEditTriggers(
		QAbstractItemView::EditTrigger::CurrentChanged);
}

OBSExtraBrowsers::~OBSExtraBrowsers()
{
	model->Apply();
	delete ui;
}

void OBSExtraBrowsers::on_apply_clicked()
{
	model->Apply();
}

/* ------------------------------------------------------------------------- */

void OBSBasic::ClearExtraBrowserDocks()
{
	extraBrowserDocks.clear();
	extraBrowserDockActions.clear();
	extraBrowserDockTargets.clear();
}

void OBSBasic::LoadExtraBrowserDocks() {}

void OBSBasic::SaveExtraBrowserDocks() {}

void OBSBasic::ManageExtraBrowserDocks()
{
	if (!extraBrowserManager.isNull()) {
		extraBrowserManager->show();
		extraBrowserManager->raise();
		return;
	}

	OBSExtraBrowsers *dlg = new OBSExtraBrowsers();
	dlg->show();
	extraBrowserManager = dlg;
}

void OBSBasic::AddExtraBrowserDock(const QString &title, const QString &url)
{
	ExtraBrowser *dock = new ExtraBrowser();
	dock->setObjectName(title);
	dock->setFeatures(QDockWidget::AllDockWidgetFeatures);
	dock->setWindowTitle(title);
	dock->setFloating(true);
	dock->setMinimumSize(50, 50);
	dock->resize(500, 500);

	QCefWidget *browser =
		cef->create_widget(nullptr, QT_TO_UTF8(url), nullptr);
	dock->SetWidget(browser);

	addDockWidget(Qt::RightDockWidgetArea, dock);
	QAction *action = AddDockWidget(dock);

	dock->setFloating(true);
	dock->setVisible(true);

	extraBrowserDocks.push_back(QSharedPointer<QDockWidget>(dock));
	extraBrowserDockActions.push_back(QSharedPointer<QAction>(action));
	extraBrowserDockTargets.push_back(url);
}
