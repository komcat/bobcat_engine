// RecipeWorker.h
#pragma once

#include <QThread>
#include <QString>

class SequenceBuilder;
class SequenceEngine;
class RecipeManager;
class ProcessSequenceFactory;

class RecipeWorker : public QThread
{
  Q_OBJECT

public:
  RecipeWorker(
    SequenceEngine* engine,
    RecipeManager* recipeManager,
    ProcessSequenceFactory* factory,
    QObject* parent = nullptr
  );

  void run() override;

signals:
  void progressUpdate(const QString& message);
  void finished(bool success, const QString& message);

private:
  void buildSequenceFromRecipe(SequenceBuilder* sequence);

  SequenceEngine* m_engine;
  RecipeManager* m_recipeManager;
  ProcessSequenceFactory* m_factory;
};