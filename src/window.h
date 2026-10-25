#pragma once
#include "market.h"
#include "monero.h"
#include "network.h"
#include "wallet_profiles.h"
#include <QElapsedTimer>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QWidget>
class QLabel;
class QLineEdit;
class QProgressBar;
class QThread;

namespace aurum {
struct Theme {
  QString name, caption;
  QColor bg, panel, panel2, line, text, muted, accent, accentText;
  int layout;
};
const QVector<Theme> &themes();
class WalletWindow : public QWidget {
  Q_OBJECT
public:
  explicit WalletWindow(bool demo = false, QWidget *parent = nullptr);
  ~WalletWindow() override;
  void setTheme(int index);
  void captureAll(const QString &directory);
  int themeIndex() const { return theme_; }

protected:
  void paintEvent(QPaintEvent *) override;
  void resizeEvent(QResizeEvent *) override;
  bool event(QEvent *) override;
  void mouseMoveEvent(QMouseEvent *) override;
  void leaveEvent(QEvent *) override;

private:
  WalletProfiles profiles_;
  Network network_;
  MoneroClient monero_;
  MarketData market_;
  bool sessionOpen_ = false;
  std::array<QByteArray, AssetCount> publicKeys_;
  QString vaultPath_;
  std::array<QString, AssetCount> addresses_;
  std::array<quint64, AssetCount> balances_{};
  bool demo_ = false, hidden_ = false;
  int theme_ = 0;
  int selectedAsset_ = 0, chartDays_ = 365;
  enum class Stage { Locked, Opening, Loading, Ready };
  Stage stage_ = Stage::Locked;
  QWidget *startupScreen_ = nullptr, *credentials_ = nullptr,
          *loading_ = nullptr;
  QLabel *startupTitle_ = nullptr, *startupSubtitle_ = nullptr,
         *unlockError_ = nullptr;
  QLabel *loadCaption_ = nullptr, *loadStatus_ = nullptr;
  std::array<QLabel *, AssetCount> loadRows_{};
  QLineEdit *startupPassword_ = nullptr, *startupConfirm_ = nullptr;
  QPushButton *startupSubmit_ = nullptr, *startupRestore_ = nullptr;
  QPushButton *loadContinue_ = nullptr;
  QProgressBar *loadProgress_ = nullptr;
  QPointer<QThread> unlockWorker_;
  QTimer loadingTimer_;
  QElapsedTimer loadingAge_;
  QString restoreSource_;
  bool chartCollapsed_ = false;
  int chartHover_ = -1;
  QPointF chartPointer_;
  QVector<QPointF> chartPoints() const;
  int chartPointAt(QPointF) const;
  QString chartHoverText() const;
  void setupStartup();
  void showLocked();
  void submitUnlock();
  void beginLoading(QByteArray moneroPassword = {});
  void updateLoading();
  void finishLoading();

  QVector<QPushButton *> buttons_;
  QRectF main_, hero_, assets_, activity_, insight_;
  QString notice_;
  void rebuild();
  void button(QRectF rect, const QString &label,
              std::function<void()> callback);
  void drawText(QPainter &, QRectF, const QString &, int, QColor,
                int weight = 400,
                Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter);
  void panel(QPainter &, QRectF, QColor, int radius = 18);
  void coin(QPainter &, QPointF, int, int size = 42);
  void chart(QPainter &, QRectF, int style);
  void drawHero(QPainter &);
  void drawExodus(QPainter &);
  void rebuildExodus();
  QRectF exodusRect(QRectF) const;
  void selectAsset(int);
  QRectF assetBounds(int) const;
  QString holdingUsd(int) const;
  double holdingValue(int) const;
  QString priceText(int) const;
  QString totalUsd() const;
  QString holdingTooltip(int) const;
  QString quoteCaption() const;
  void drawAssets(QPainter &);
  void drawActivity(QPainter &);
  void drawInsight(QPainter &);
  void drawNavigation(QPainter &);
  void unlockWallet();
  void backupWallet();
  void receive(int selected = 0);
  void send(int selected = 0);
  void swap(int selected = 0);
  void sendMonero();
  void sendAccountAsset(Chain);
  void assetList();
  void settings();
  void activity();
  void lockWallet();
  void walletManager();
  void switchWallet(const QString &id);
  void notify(const QString &);
  QString amount(int chain) const;
  QString uiStyle() const;
  void themePicker();
};
} // namespace aurum
