# GitHub Pages deployment

[日本語 (JA)](#ja) | [English (EN)](#en)

## JA

PagesはWeb Serial用の公開先です。USB Ethernet版のdevice-hosted assetsとは配信経路が異なります。
公開済みPagesがローカルcheckoutと同じrevisionとは限りません。
以下は既存workflowに対応する公開手順です。

すべてのコマンドは、特記がなければrepository rootで実行します。
[開発資料一覧](README.md) · [プロジェクト概要](../../README.md)

### GitHub Pages deployment

[.github/workflows/pages.yml](../../.github/workflows/pages.yml)はGitHub公式の
configure-pages / upload-pages-artifact / deploy-pagesを使い、**configurator/だけ**を公開します。
CSS / JSはrelative pathなので、`https://<user>.github.io/<repository>/`のproject Pagesにも対応します。
PagesのHTTPSはWeb Serialのsecure context要件を満たします。backendやbuild toolは不要です。

Maintainer向けGitHub設定手順:

1. GitHub repositoryを用意し、任意のremoteを設定して変更をpushする。
2. repositoryの**Settings → Pages → Build and deployment → Source**で**GitHub Actions**を選ぶ。
3. **Settings → Actions → General**でActionsと使用するGitHub公式Actionsが許可されていることを確認する。
   組織のpolicyでPages / OIDCが制限されている場合は管理者に確認する。
4. workflowをdefault branchへ配置する。default branchはrepository情報から判定するため、main固定ではない。
5. **Actions → Deploy Configurator to GitHub Pages → Run workflow**でdefault branchを選び初回deployする。
   以後はdefault branchの`configurator/**`、Webテスト、Pages workflowの変更で自動deployする。
   他branchでの実行はjobをskipする。
6. `github-pages` environmentに承認ルールがある場合は承認する。branch制限を使う場合はdefault branchを許可する。
7. Actionsのdeployment URLまたはSettings → Pagesの公開URLを開く。

workflowはWebテストに成功してからartifactをuploadし、deployします。
`contents: read`、`pages: write`、`id-token: write`を使用します。
環境のPages設定・repository設定は自動変更しません。README / firmware / testsは公開artifactに含めません。
GitHubプランやrepository可視性に応じてPagesを利用可能なrepositoryを用意してください。
手順の根拠は[GitHub公式Pages workflow資料](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages)です。

## EN

Pages publishes the Web Serial frontend; it is a different delivery path from device-hosted USB Ethernet assets. The published revision may differ from the local checkout.
The following procedure describes the existing workflow.

Run commands from the repository root unless stated otherwise.
[Development index](README.md) · [Project overview](../../README.md)

### GitHub Pages deployment

[.github/workflows/pages.yml](../../.github/workflows/pages.yml) uses the official configure-pages / upload-pages-artifact / deploy-pages actions and publishes **only configurator/**.
Relative CSS/JS paths support project Pages at `https://<user>.github.io/<repository>/`. Pages HTTPS satisfies Web Serial's secure-context requirement. No backend or build tool is needed.

GitHub setup steps for maintainers:

1. Create a repository, configure the desired remote, and push changes when publication is intended.
2. In **Settings → Pages → Build and deployment → Source**, choose **GitHub Actions**.
3. Under **Settings → Actions → General**, allow Actions and the required official actions. Consult the organization administrator if policy restricts Pages or OIDC.
4. Place the workflow on the default branch. The workflow detects that branch rather than hard-coding main.
5. In **Actions → Deploy Configurator to GitHub Pages → Run workflow**, select the default branch for the initial deployment. Subsequent default-branch changes to `configurator/**`, Web tests, or the Pages workflow deploy automatically. Jobs on other branches are skipped.
6. Approve the `github-pages` environment if required. Branch restrictions must permit the default branch.
7. Open the deployment URL from Actions or Settings → Pages.

The workflow runs Web tests before uploading/deploying the artifact. It uses `contents: read`, `pages: write`, and `id-token: write`. It does not change repository/Pages settings automatically. README, firmware, and tests are excluded from the artifact.
Use a repository eligible for Pages under its visibility and GitHub plan. Reference: [official Pages custom-workflow documentation](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages).
